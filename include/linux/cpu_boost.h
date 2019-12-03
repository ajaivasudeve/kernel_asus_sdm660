#ifndef _CPU_BOOST_H_
#define _CPU_BOOST_H_

#ifdef CONFIG_CPU_BOOST
void do_dsb_kick(void);
#else
static inline void do_dsb_kick(void)
{
}
#endif
#endif 