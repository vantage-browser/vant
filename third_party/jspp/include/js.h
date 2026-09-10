#ifndef JS_H
#define JS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(JS_BUILD_SHARED)
# define JS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(JS_USE_SHARED)
# define JS_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
# define JS_API __attribute__((visibility("default")))
#else
# define JS_API
#endif

#define JS_API_VERSION_MAJOR 0
#define JS_API_VERSION_MINOR 1
#define JS_API_VERSION ((JS_API_VERSION_MAJOR << 16) | JS_API_VERSION_MINOR)

typedef struct js_runtime js_runtime;
typedef struct js_value js_value;
typedef enum js_status {
    JS_STATUS_OK=0,
    JS_STATUS_INVALID_ARGUMENT=1,
    JS_STATUS_WRONG_THREAD=2,
    JS_STATUS_UNSUPPORTED=3,
    JS_STATUS_SYNTAX_ERROR=4,
    JS_STATUS_RUNTIME_ERROR=5,
    JS_STATUS_OUT_OF_MEMORY=6,
    JS_STATUS_LIMIT_EXCEEDED=7
} js_status;
typedef enum js_value_kind {
    JS_VALUE_UNDEFINED=0, JS_VALUE_NULL=1, JS_VALUE_BOOLEAN=2,
    JS_VALUE_NUMBER=3, JS_VALUE_STRING=4, JS_VALUE_FUNCTION=5,
    JS_VALUE_OBJECT=6, JS_VALUE_ARRAY=7
} js_value_kind;
typedef js_status (*js_native_callback)(js_runtime *runtime,
    const js_value *this_value,const js_value *const *arguments,size_t argument_count,
    void *user_data,js_value **result);
/* Native callback arguments are borrowed; a successful result transfers one
   owning handle to the engine. */

JS_API const char *js_version(void);
JS_API unsigned int js_api_version(void);
JS_API js_runtime *js_runtime_new(void);
JS_API void js_runtime_free(js_runtime *runtime);
JS_API const char *js_runtime_last_error(const js_runtime *runtime);
JS_API void js_runtime_set_error(js_runtime *runtime,const char *message);
JS_API js_status js_runtime_get_exception(js_runtime *runtime,js_value **result);
JS_API js_status js_runtime_set_global(js_runtime *runtime,const char *name,size_t name_size,const js_value *value);
JS_API js_status js_runtime_get_global(js_runtime *runtime,const char *name,size_t name_size,js_value **result);
JS_API js_status js_runtime_set_instruction_limit(js_runtime *runtime,size_t limit);
JS_API js_status js_runtime_set_stack_limit(js_runtime *runtime,size_t limit);
JS_API js_status js_runtime_set_allocation_limit(js_runtime *runtime,size_t limit);
JS_API js_status js_runtime_get_allocation_count(js_runtime *runtime,size_t *count);
JS_API js_status js_eval(js_runtime *runtime,const char *source,js_value **result);
JS_API void js_value_free(js_runtime *runtime,js_value *value);
JS_API js_value_kind js_value_get_kind(const js_value *value);
JS_API int js_value_get_boolean(const js_value *value,int *out);
JS_API int js_value_get_number(const js_value *value,double *out);
JS_API int js_value_get_string(const js_value *value,const char **data,size_t *size);

/* Every value returned through js_value ** is an owning runtime handle. */
JS_API js_status js_value_new_undefined(js_runtime *runtime,js_value **result);
JS_API js_status js_value_new_null(js_runtime *runtime,js_value **result);
JS_API js_status js_value_new_boolean(js_runtime *runtime,int value,js_value **result);
JS_API js_status js_value_new_number(js_runtime *runtime,double value,js_value **result);
JS_API js_status js_value_new_string(js_runtime *runtime,const char *data,size_t size,js_value **result);
JS_API js_status js_object_new(js_runtime *runtime,js_value **result);
JS_API js_status js_array_new(js_runtime *runtime,js_value **result);
JS_API js_status js_object_set(js_runtime *runtime,js_value *object,const char *name,size_t name_size,const js_value *value);
JS_API js_status js_object_get(js_runtime *runtime,const js_value *object,const char *name,size_t name_size,js_value **result);
JS_API js_status js_array_set(js_runtime *runtime,js_value *array,size_t index,const js_value *value);
JS_API js_status js_array_get(js_runtime *runtime,const js_value *array,size_t index,js_value **result);
JS_API js_status js_array_get_length(js_runtime *runtime,const js_value *array,size_t *length);
JS_API js_status js_function_new_native(js_runtime *runtime,const char *name,size_t name_size,
    js_native_callback callback,void *user_data,js_value **result);
JS_API js_status js_call(js_runtime *runtime,const js_value *function,const js_value *this_value,
    const js_value *const *arguments,size_t argument_count,js_value **result);

#ifdef __cplusplus
}
#endif
#endif
