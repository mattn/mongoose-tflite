#include <vector>
#include <chrono>
#include <iostream>
#include <iostream>
#include <fstream>
#include <opencv2/opencv.hpp>

#include <tensorflow/lite/model.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>

#include "detect.h"

template<typename T>
void
fill(T *in, cv::Mat& src) {
  int n = 0, nc = src.channels(), ne = src.elemSize();
  for (int y = 0; y < src.rows; ++y)
    for (int x = 0; x < src.cols; ++x)
      for (int c = 0; c < nc; ++c)
        in[n++] = (((float)src.data[y * src.step + x * ne + (nc - c)]) - 127.5) / 127.5;
        //in[n++] = ((float)src.data[y * src.step + x * ne + (nc - c)]) / 255;
}

static std::unique_ptr<tflite::FlatBufferModel> model;
static std::unique_ptr<tflite::Interpreter> interpreter;
static int wanted_height;
static int wanted_width;
static int wanted_channels;
static int wanted_type;
static uint8_t *in8 = nullptr;
static float *in16 = nullptr;

extern "C" int
initialize_detect(const char* modelfile) {
  TfLiteStatus status;
  tflite::ops::builtin::BuiltinOpResolver resolver;
  model = tflite::FlatBufferModel::BuildFromFile(modelfile);
  if (!model) {
    return -1;
  }
  tflite::InterpreterBuilder(*model, resolver)(&interpreter);
  status = interpreter->AllocateTensors();

  if (status != kTfLiteOk) {
    std::cerr << "Failed to allocate the memory for tensors." << std::endl;
    return -2;
  }

  int input = interpreter->inputs()[0];

  TfLiteIntArray* dims = interpreter->tensor(input)->dims;
  wanted_height = dims->data[1];
  wanted_width = dims->data[2];
  wanted_channels = dims->data[3];
  wanted_type = interpreter->tensor(input)->type;

  if (wanted_type == kTfLiteFloat32) {
    in16 = interpreter->typed_tensor<float>(input);
  } else if (wanted_type == kTfLiteUInt8) {
    in8 = interpreter->typed_tensor<uint8_t>(input);
  }

  interpreter->SetNumThreads(4);
  interpreter->UseNNAPI(0);
  return 0;
}

extern "C" int 
detect_object(
    unsigned char *data,
    unsigned long len,
    DETECT_RESULT *detect_results,
    int nresults) {
  TfLiteStatus status;
  std::vector<uchar> buf;
  buf.assign(data, data + len);
  cv::Mat frame = cv::imdecode(cv::Mat(buf), -1);
  if (frame.empty()){
    return -1;
  }
  cv::Mat resized(wanted_height, wanted_width, frame.type());
  cv::resize(frame, resized, resized.size(), cv::INTER_CUBIC);

  if (wanted_type == kTfLiteFloat32) {
    fill(in16, resized);
  } else if (wanted_type == kTfLiteUInt8) {
    fill(in8, resized);
  }

  status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    std::cerr << "ERROR" << std::endl;
    return -2;
  }

  int output = interpreter->outputs()[0];
  TfLiteIntArray* output_dims = interpreter->tensor(output)->dims;
  auto output_size = output_dims->data[output_dims->size - 1];
  int output_type = interpreter->tensor(output)->type;

  std::vector<std::pair<float, int>> results;

  if (wanted_type == kTfLiteFloat32) {
    float *scores = interpreter->typed_output_tensor<float>(0);
    for (int i = 0; i < output_size; ++i) {
      float value = scores[i];
      if (value < 0.1)
        continue;
      results.push_back(std::pair<float, int>(value, i));
    }
  } else if (wanted_type == kTfLiteUInt8) {
    uint8_t *scores = interpreter->typed_output_tensor<uint8_t>(0);
    for (int i = 0; i < output_size; ++i) {
      printf("%d\n", scores[i]);
      float value = ((float)scores[i]) / 255.0;
      //if (value < 0.2)
        //continue;
      results.push_back(std::pair<float, int>(value, i));
    }
  }
  std::sort(results.begin(), results.end(),
    [](std::pair<float, int>& x, std::pair<float, int>& y) -> int {
      return x.first > y.first;
    }
  );

  int i;
  for (i = 0; i < nresults && i < results.size(); i++) {
    detect_results[i].probability = results[i].first;
    detect_results[i].index = results[i].second;
  }
  return i;
}

#ifdef MAIN
int
main(int argc, char const * argv[]) {
  if (initialize_detect("output_graph.tflite") < 0) {
    std::cerr << "Failed to initialize object detection" << std::endl;
    return -1;
  }

  std::ifstream imagefile(argv[1], std::ios::in | std::ios::binary);
  if (!imagefile) {
    std::cerr << "Failed to read image file" << std::endl;
    return -1;
  }
  std::vector<uint8_t> imagedata(
      (std::istreambuf_iterator<char>(imagefile)),
      std::istreambuf_iterator<char>());

  std::ifstream labelfile("output_labels.txt");
  if (!labelfile) {
    std::cerr << "Failed to read label file" << std::endl;
    return -1;
  }
  std::vector<std::string> labels;
  std::string line;
  while (std::getline(labelfile, line))
    labels.push_back(line);
  while (labels.size() % 16)
    labels.emplace_back();

  DETECT_RESULT results[5];
  int nresult = detect_object(
      imagedata.data(),
      imagedata.size(),
      &results[0], 5);
  if (nresult < 0) {
    std::cerr << "Failed to detect object" << std::endl;
    return -1;
  }
  
  for (int i = 0; i < nresult; i++) {
    std::cout << results[i].probability << ": " << labels[results[i].index] << std::endl;
  }
}
#endif

// vim:set cino=>2 et:
