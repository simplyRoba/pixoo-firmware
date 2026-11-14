#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
void loop_task(void *pv_params);
}

extern "C" TaskHandle_t __real_xTaskCreateStaticPinnedToCore(
    TaskFunction_t task, const char *const name, const uint32_t stack_depth,
    void *const parameters, UBaseType_t priority, StackType_t *const stack,
    StaticTask_t *const task_buffer, const BaseType_t core);

extern "C" TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(
    TaskFunction_t task, const char *const name, const uint32_t stack_depth,
    void *const parameters, UBaseType_t priority, StackType_t *const stack,
    StaticTask_t *const task_buffer, const BaseType_t core) {
  return __real_xTaskCreateStaticPinnedToCore(
      task, name, stack_depth, parameters, priority, stack, task_buffer,
      task == &esphome::loop_task ? 0 : core);
}
