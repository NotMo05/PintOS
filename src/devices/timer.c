#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <string.h>

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* struct for storing wakeup information of threads */
struct wakeup_elem {
    struct list_elem elem;
    struct semaphore sleep_semaphore;
    int64_t wakeup_time;
};

/* list of wakeup_elems for non-busy sleep */
static struct list wakeup_info;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

/* function for sorting wakeup_elem's based on the wakeup_time */
static bool sort_by_wakeup_time(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED) {
    const struct wakeup_elem *a = list_entry(a_, struct wakeup_elem, elem);
    const struct wakeup_elem *b = list_entry(b_, struct wakeup_elem, elem);
    return a->wakeup_time < b->wakeup_time;
}

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void)
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  /* Initialising list of wakeup_elems */
  list_init (&wakeup_info);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void)
{
    unsigned high_bit, test_bit;

    ASSERT (intr_get_level () == INTR_ON);
    printf ("Calibrating timer...  ");

    /* Approximate loops_per_tick as the largest power-of-two
       still less than one timer tick. */
    loops_per_tick = 1u << 10;
    while (!too_many_loops (loops_per_tick << 1))
    {
        loops_per_tick <<= 1;
        ASSERT (loops_per_tick != 0);
    }

    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops (high_bit | test_bit))
            loops_per_tick |= test_bit;

    printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void)
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then)
{
  return timer_ticks () - then;
}

/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  if (ticks <= 0)
    {
      return;
    }

  int64_t start = timer_ticks ();
  ASSERT (intr_get_level () == INTR_ON);
  /* We don't sleep if the time elapsed since the initialisation of `start`
      is less than the time we were meant to sleep the thread anyways. */
  if (timer_elapsed (start) < ticks)
    {
      /* Declaring a wakeup_elem and a sleep/waking-up semaphore. */
      struct wakeup_elem wakeupElem;
      struct semaphore wakeup_semaphore;
      /* Setting the wakeup time, and setting and initialising the
          wakeup_elem's associated sleep_semaphore. */
      wakeupElem.wakeup_time = start + ticks;
      wakeupElem.sleep_semaphore = wakeup_semaphore;
      sema_init (&wakeupElem.sleep_semaphore, 0);
      /* Inserting into the list of sleeping threads */
      enum intr_level old_level = intr_disable ();
      list_insert_ordered (
        &wakeup_info, &wakeupElem.elem, sort_by_wakeup_time, NULL
      );
      intr_set_level (old_level);
      /* Block thread via semaphore */
      sema_down (&wakeupElem.sleep_semaphore);
    }
}

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms)
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us)
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns)
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms)
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us)
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns)
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void)
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Calculates and sets the load_avg value.
   PRE: Must be called with interrupts turned off. */
static void
calc_set_load_avg (void)
{
  fixed_point load_avg_coeff = div_fp_int (conv_int_to_fp (59), 60);
  fixed_point ready_threads_coeff = div_fp_int (conv_int_to_fp (1), 60);
  int ready_threads = threads_ready ();
  if (!is_idle_thread ())
    {
      ready_threads += 1;
    }
  fixed_point first_term = mult_fp_fp (load_avg_coeff, load_avg);
  fixed_point second_term = mult_fp_int (ready_threads_coeff, ready_threads);
  load_avg = add_fp_fp (first_term, second_term);
}

/* An action function that calculates and sets the recent_cpu value of the given
   thread.
   PRE: Must be called with interrupts turned off. */
static void
calc_set_recent_cpu_action (struct thread *t, void *aux UNUSED)
{
  t->prev_recent_cpu = t->recent_cpu;
  fixed_point rec_cpu_coeff = div_fp_fp (
    mult_fp_int (load_avg, 2),
    add_fp_int (mult_fp_int (load_avg, 2), 1)
  );
  t->recent_cpu = add_fp_int (
    mult_fp_fp (rec_cpu_coeff, t->recent_cpu), t->nice
  );
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  /* Disabling interrupts like this is unnecessary as interrupts are already
      turned off when running an interrupt handler. */
  enum intr_level old_level = intr_disable();
  if (thread_mlfqs) // Advanced scheduling
    {
      struct thread *cur = thread_current ();
      /* Setting the previous recent_cpu and incrementing the recent_cpu of
          the current thread if it isn't the idle thread  */
      if (!is_idle_thread ())
        {
          cur->prev_recent_cpu = cur->recent_cpu;
          cur->recent_cpu = add_fp_int (cur->recent_cpu, 1);
        }
      /* Recalculating the load_avg and recalculating the recent_cpu of all
          threads every second. */
      if (timer_ticks () % TIMER_FREQ == 0)
        {
          calc_set_load_avg ();
          thread_foreach (calc_set_recent_cpu_action, NULL);
        }
      /* Recalculating the priority of all threads every 4 ticks. */
      if (timer_ticks () % PRIORITY_CALC_DELAY == 0)
        {
          /* Priority is recalculated for each thread (if necessary) on every
              4th clock tick. Yielding (on return of the interrupt handler) is
              done without checking the highest priority runnable thread as
              yielding calls schedule which already checks if the next thread
              to run is the same thread in which case it does not switch
              threads. */
          thread_foreach (calc_prio_sort_action, NULL);
          intr_yield_on_return ();
        }
    } // Advanced scheduling and priority scheduling
  /* Iterating wakeup_info list, since it's sorted we can use while loop with
     list_pop_front. */
  while (!list_empty (&wakeup_info))
    {
      struct wakeup_elem *w_elem = list_entry (
        list_begin (&wakeup_info), struct wakeup_elem, elem
      );
      int64_t wakeup_time = w_elem->wakeup_time;
      if (timer_ticks() >= wakeup_time)
        {
          sema_up (&w_elem->sleep_semaphore);
          list_pop_front (&wakeup_info);
        }
      else
        {
          break;
        }
    }
  intr_set_level (old_level);
  thread_tick ();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops)
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
      barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops)
{
  while (loops-- > 0)
  barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom)
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.

          (NUM / DENOM) s
      ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
      1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
          timer_sleep() because it will yield the CPU to other
          processes. */
      timer_sleep (ticks);
    }
  else
    {
      /* Otherwise, use a busy-wait loop for more accurate
          sub-tick timing. */
      real_time_delay (num, denom);
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
      the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
}
