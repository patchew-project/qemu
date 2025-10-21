#ifndef HW_AUDIO_MODEL_H
#define HW_AUDIO_MODEL_H

void audio_register_model_with_cb(const char *name, const char *descr,
                                  int (*init_audio_model)(const char *audiodev));
void audio_register_model(const char *name, const char *descr,
                          int isa, const char *typename);

void audio_model_init(void);
void audio_print_available_models(void);
void audio_set_model(const char *name, const char *audiodev);

#endif
