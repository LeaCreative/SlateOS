#include <cstdint>

#include "boot_diag.hpp"
#include "nrf52832_regs.hpp"

extern "C" {
extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;
extern std::uint32_t _sbss;
extern std::uint32_t _ebss;
extern std::uint32_t __StackTop;
extern void (*__preinit_array_start[])();
extern void (*__preinit_array_end[])();
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

int main();
void SystemInit();
void Default_Handler();
void slate_relocate_vtor();
}

extern "C" void Default_Handler() {
  // Any IRQ still aliased here — or shifted onto this entry by a short vector
  // table — previously froze the last frame with no colour until the bootloader
  // WDT reverted. Paint magenta and starve WDT so the cause is visible ~15 s.
  boot_diag::fatal_paint_and_hang(boot_diag::kMagenta);
}
extern "C" void NMI_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void HardFault_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void MemManage_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void BusFault_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void UsageFault_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SVC_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void DebugMon_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void PendSV_Handler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SysTick_Handler() __attribute__((weak, alias("Default_Handler")));

extern "C" void POWER_CLOCK_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void RADIO_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void UARTE0_UART0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void NFCT_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void GPIOTE_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SAADC_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TIMER0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TIMER1_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TIMER2_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void RTC0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TEMP_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void RNG_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void ECB_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void CCM_AAR_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void WDT_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void RTC1_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void RTC2_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void QDEC_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void COMP_LPCOMP_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI0_EGU0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI1_EGU1_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI2_EGU2_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI3_EGU3_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI4_EGU4_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SWI5_EGU5_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TIMER3_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void TIMER4_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void PWM0_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void PDM_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void MWU_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void PWM1_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void PWM2_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void SPIM2_SPIS2_SPI2_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void I2S_IRQHandler() __attribute__((weak, alias("Default_Handler")));
extern "C" void FPU_IRQHandler() __attribute__((weak, alias("Default_Handler")));

extern "C" [[noreturn]] void Reset_Handler() {
  auto* src = &_sidata;
  auto* dst = &_sdata;
  while (dst < &_edata) {
    *dst++ = *src++;
  }

  dst = &_sbss;
  while (dst < &_ebss) {
    *dst++ = 0u;
  }

  SystemInit();
  // VTOR must be 256-byte aligned. App image sits at 0x8020 (after MCUBoot header);
  // InfiniTime BL relocates, but we always install our own copy so RTC/GPIO IRQs work.
  slate_relocate_vtor();
  for (auto ctor = __preinit_array_start; ctor < __preinit_array_end; ++ctor) {
    (*ctor)();
  }
  for (auto ctor = __init_array_start; ctor < __init_array_end; ++ctor) {
    (*ctor)();
  }
  (void)main();

  while (true) {
  }
}

using Isr = void (*)();

// `used` + linker KEEP(*(.isr_vector*)) — required with -fdata-sections/--gc-sections.
__attribute__((used, section(".isr_vector")))
const Isr interrupt_vector[] = {
  reinterpret_cast<Isr>(&__StackTop),
  Reset_Handler,
  NMI_Handler,
  HardFault_Handler,
  MemManage_Handler,
  BusFault_Handler,
  UsageFault_Handler,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  SVC_Handler,
  DebugMon_Handler,
  nullptr,
  PendSV_Handler,
  SysTick_Handler,
  POWER_CLOCK_IRQHandler,
  RADIO_IRQHandler,
  UARTE0_UART0_IRQHandler,
  SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler,
  SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQHandler,
  NFCT_IRQHandler,
  GPIOTE_IRQHandler,
  SAADC_IRQHandler,
  TIMER0_IRQHandler,
  TIMER1_IRQHandler,
  TIMER2_IRQHandler,
  RTC0_IRQHandler,
  TEMP_IRQHandler,
  RNG_IRQHandler,
  ECB_IRQHandler,
  CCM_AAR_IRQHandler,
  WDT_IRQHandler,
  RTC1_IRQHandler,
  QDEC_IRQHandler,
  COMP_LPCOMP_IRQHandler,
  SWI0_EGU0_IRQHandler,
  SWI1_EGU1_IRQHandler,
  SWI2_EGU2_IRQHandler,
  SWI3_EGU3_IRQHandler,
  SWI4_EGU4_IRQHandler,
  SWI5_EGU5_IRQHandler,
  TIMER3_IRQHandler,
  TIMER4_IRQHandler,
  PWM0_IRQHandler,
  PDM_IRQHandler,
  // IRQs 30 and 31 are reserved on nRF52832. Omitting either shifts MWU..FPU
  // so RTC2 (IRQ 36, enabled in power::init) lands on Default_Handler and the
  // watch freezes with the last frame until the bootloader WDT reverts it.
  nullptr,
  nullptr,
  MWU_IRQHandler,
  PWM1_IRQHandler,
  PWM2_IRQHandler,
  SPIM2_SPIS2_SPI2_IRQHandler,
  RTC2_IRQHandler,
  I2S_IRQHandler,
  FPU_IRQHandler,
};

namespace {

alignas(256) std::uint32_t g_vtable_ram[sizeof(interrupt_vector) / sizeof(Isr)];

}  // namespace

extern "C" void slate_relocate_vtor() {
  const auto* src = reinterpret_cast<const std::uint32_t*>(interrupt_vector);
  for (std::size_t i = 0; i < sizeof(g_vtable_ram) / sizeof(g_vtable_ram[0]); ++i) {
    g_vtable_ram[i] = src[i];
  }
  constexpr std::uintptr_t kVtor = 0xE000ED08u;
  *reinterpret_cast<volatile std::uint32_t*>(kVtor) =
      reinterpret_cast<std::uint32_t>(g_vtable_ram);
  asm volatile("dsb" ::: "memory");
  asm volatile("isb" ::: "memory");
}
