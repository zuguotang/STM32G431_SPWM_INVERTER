#include "stm32g4xx.h"
#include "inverter_config.h"

uint32_t SystemCoreClock = 16000000UL;

const uint8_t AHBPrescTable[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9
};

const uint8_t APBPrescTable[8] = {
    0, 0, 0, 0, 1, 2, 3, 4
};

void SystemInit(void)
{
#if defined(__FPU_PRESENT) && (__FPU_PRESENT == 1U)
    SCB->CPACR |= ((3UL << 20U) | (3UL << 22U));
#endif
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = SYSCLK_HZ;
}
