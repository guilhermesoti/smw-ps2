/* Relogio do sistema do PS2SDK simulado (ciclos do barramento, 147,456 MHz). */
#ifndef TEST_TIMER_H
#define TEST_TIMER_H
typedef unsigned int u32;
typedef unsigned long long u64;
static u64 teste_relogio;
static inline u64 GetTimerSystemTime(void) { return teste_relogio; }
static inline void TimerBusClock2USec(u64 c, u32 *s, u32 *us) {
  *s = (u32)(c / 147456000u);
  *us = (u32)(c % 147456000u * 1000000u / 147456000u);
}
#endif
