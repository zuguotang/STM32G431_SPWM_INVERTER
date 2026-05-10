.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global g_pfnVectors
.global Reset_Handler

.word _sidata
.word _sdata
.word _edata
.word _sbss
.word _ebss

.section .text.Reset_Handler
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
  ldr sp, =_estack
  bl SystemInit
  ldr r0, =_sdata
  ldr r1, =_edata
  ldr r2, =_sidata
copy_data:
  cmp r0, r1
  bcc copy_data_loop
  b zero_bss
copy_data_loop:
  ldr r3, [r2], #4
  str r3, [r0], #4
  b copy_data
zero_bss:
  ldr r0, =_sbss
  ldr r1, =_ebss
  movs r2, #0
zero_bss_loop:
  cmp r0, r1
  bcc zero_bss_store
  b call_main
zero_bss_store:
  str r2, [r0], #4
  b zero_bss_loop
call_main:
  bl main
  b .

.weak NMI_Handler
.thumb_set NMI_Handler,Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler,Default_Handler
.weak MemManage_Handler
.thumb_set MemManage_Handler,Default_Handler
.weak BusFault_Handler
.thumb_set BusFault_Handler,Default_Handler
.weak UsageFault_Handler
.thumb_set UsageFault_Handler,Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler,Default_Handler
.weak DebugMon_Handler
.thumb_set DebugMon_Handler,Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler,Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler,Default_Handler
.weak DMA1_Channel1_IRQHandler
.thumb_set DMA1_Channel1_IRQHandler,Default_Handler
.weak ADC1_2_IRQHandler
.thumb_set ADC1_2_IRQHandler,Default_Handler
.weak TIM1_BRK_TIM15_IRQHandler
.thumb_set TIM1_BRK_TIM15_IRQHandler,Default_Handler
.weak TIM1_UP_TIM16_IRQHandler
.thumb_set TIM1_UP_TIM16_IRQHandler,Default_Handler
.weak TIM6_DAC_IRQHandler
.thumb_set TIM6_DAC_IRQHandler,Default_Handler

.section .text.Default_Handler,"ax",%progbits
Default_Handler:
  b .

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler
  .word 0 /* WWDG */
  .word 0 /* PVD_PVM */
  .word 0 /* RTC_TAMP_LSECSS */
  .word 0 /* RTC_WKUP */
  .word 0 /* FLASH */
  .word 0 /* RCC */
  .word 0 /* EXTI0 */
  .word 0 /* EXTI1 */
  .word 0 /* EXTI2 */
  .word 0 /* EXTI3 */
  .word 0 /* EXTI4 */
  .word DMA1_Channel1_IRQHandler
  .word 0 /* DMA1_Channel2 */
  .word 0 /* DMA1_Channel3 */
  .word 0 /* DMA1_Channel4 */
  .word 0 /* DMA1_Channel5 */
  .word 0 /* DMA1_Channel6 */
  .word 0 /* DMA1_Channel7 */
  .word ADC1_2_IRQHandler
  .word 0 /* USB_HP */
  .word 0 /* USB_LP */
  .word 0 /* FDCAN1_IT0 */
  .word 0 /* FDCAN1_IT1 */
  .word 0 /* EXTI9_5 */
  .word TIM1_BRK_TIM15_IRQHandler
  .word TIM1_UP_TIM16_IRQHandler
  .word 0 /* TIM1_TRG_COM_TIM17 */
  .word 0 /* TIM1_CC */
  .word 0 /* TIM2 */
  .word 0 /* TIM3 */
  .word 0 /* TIM4 */
  .word 0 /* I2C1_EV */
  .word 0 /* I2C1_ER */
  .word 0 /* I2C2_EV */
  .word 0 /* I2C2_ER */
  .word 0 /* SPI1 */
  .word 0 /* SPI2 */
  .word 0 /* USART1 */
  .word 0 /* USART2 */
  .word 0 /* USART3 */
  .word 0 /* EXTI15_10 */
  .word 0 /* RTC_Alarm */
  .word 0 /* USBWakeUp */
  .word 0 /* TIM8_BRK */
  .word 0 /* TIM8_UP */
  .word 0 /* TIM8_TRG_COM */
  .word 0 /* TIM8_CC */
  .word 0 /* ADC3 */
  .word 0 /* FMC */
  .word 0 /* LPTIM1 */
  .word 0 /* TIM5 */
  .word 0 /* SPI3 */
  .word 0 /* UART4 */
  .word 0 /* UART5 */
  .word TIM6_DAC_IRQHandler
  .space (118 * 4)
.size g_pfnVectors, .-g_pfnVectors
