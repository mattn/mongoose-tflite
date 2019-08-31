#ifndef DETECT_H
#define DETECT_H
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
  int index;
  float probability;
} DETECT_RESULT;

int initialize_detect(const char* modelfile);
int detect_object(unsigned char *data, unsigned long len, DETECT_RESULT *detect_results, int nresults);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* DETECT_H */
