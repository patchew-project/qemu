#ifndef HW_AUDIO_MODEL_H
#define HW_AUDIO_MODEL_H

void audio_register_model(const char *name, const char *descr,
                          const char *typename);

void audio_model_init(void);
void audio_print_available_models(void);
void audio_set_model(const char *name, const char *audiodev);

#endif
