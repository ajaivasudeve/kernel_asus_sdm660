#ifndef _CPU_BOOST_H_
#define _CPU_BOOST_H_

#ifdef CONFIG_CPU_BOOST
void do_dsb_kick(void);
extern unsigned long last_input_time;
void input_boost_max_kick(unsigned int duration_ms);
#else
static inline void do_dsb_kick(void)
{
}
static inline void input_boost_max_kick(unsigned int duration_ms)
{
}
#endif
#endif 