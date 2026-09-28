#ifndef EVOLUTIONARY_KERNEL_H
#define EVOLUTIONARY_KERNEL_H

#include <stdint.h>

#define GENOME_SIZE 256   // <-- definición necesaria para el orquestador

#ifdef __cplusplus
extern "C" {
#endif

void evo_initialize_population(void);
void evo_evolve_generation(void);
float evo_best_fitness(void);
void evo_get_best_genome(uint8_t* out_genome, int len);
int  evo_get_generation(void);
void evo_set_save_path(const char* path);
int  evo_save_state(void);
int  evo_load_state(void);
void evo_update_audio_cues(float loudness, float transient, float spatial);
void evo_set_mutation_rate(float rate);
float evo_get_mutation_rate(void);

#ifdef __cplusplus
}
#endif

#endif // EVOLUTIONARY_KERNEL_H
