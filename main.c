#include "mongoose.h"
#include "parson.h"
#include "detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static JSON_Array *labels_array;

const char *base_path = ".";
static const char *s_http_port = "5000";
static struct mg_serve_http_opts s_http_server_opts;
static int s_received_signal = 0;

struct file_writer_data {
  unsigned char *data;
  unsigned long bytes_written;
};

static void
handle_upload(struct mg_connection *nc, int ev, void *p) {
  struct file_writer_data *data = (struct file_writer_data *) nc->user_data;
  struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;

  switch (ev) {
    case MG_EV_HTTP_REQUEST:
      mg_printf(nc, "HTTP/1.1 302 Found\r\n"
          "Location: /\r\n\r\n");
      nc->flags |= MG_F_SEND_AND_CLOSE;
      return;
      break;
    case MG_EV_HTTP_PART_BEGIN: 
      if (strcmp(mp->var_name, "file") != 0) return;
      if (data == NULL) {
        data = calloc(1, sizeof(struct file_writer_data));
        data->data = NULL;
        data->bytes_written = 0;
        nc->user_data = (void *) data;
      }
      break;
    case MG_EV_HTTP_PART_DATA:
      if (data != NULL) {
        unsigned char *new_data = realloc(data->data, data->bytes_written + mp->data.len);
        if (!new_data) {
          mg_printf(nc, "HTTP/1.1 500 Failed to write to a file\r\n"
              "Content-Length: 0\r\n\r\n");
          nc->flags |= MG_F_SEND_AND_CLOSE;
          free(data->data);
          free(data);
          nc->user_data = NULL;
          return;
        }
        data->data = new_data;
        memcpy(data->data + data->bytes_written, mp->data.p, mp->data.len);
        data->bytes_written += mp->data.len;
      }
      break;
    case MG_EV_HTTP_PART_END:
      if (data != NULL) {
        DETECT_RESULT results[5] = {0};
        int nresult;
        nresult = detect_object(
            data->data, data->bytes_written, &results[0], 5);
        if (nresult < 0) {
          mg_printf(nc, "HTTP/1.1 500 Failed to write to a file\r\n"
              "Content-Length: 0\r\n\r\n");
          nc->flags |= MG_F_SEND_AND_CLOSE;
          free(data->data);
          free(data);
          nc->user_data = NULL;
          return;
        }
        JSON_Value *root_value = json_value_init_array();
        JSON_Array *root_array = json_value_get_array(root_value);
        for (int i = 0; i < nresult; i++) {
          JSON_Value *label_value = json_value_init_object();
          JSON_Object *label_object = json_value_get_object(label_value);
          json_object_set_string(label_object, "label", json_array_get_string(labels_array, results[i].index));
          json_object_set_number(label_object, "probability", results[i].probability);
          json_array_append_value(root_array, label_value);
        }
        char *serialized_string = json_serialize_to_string(root_value);
		if (serialized_string) {
          mg_printf(nc, "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %lu\r\n\r\n%s",
              (unsigned long) strlen(serialized_string),
              serialized_string);
          json_free_serialized_string(serialized_string);
        } else {
          mg_printf(nc, "HTTP/1.1 500 Failed to write to a file\r\n"
              "Content-Length: 0\r\n\r\n");
		}
        json_value_free(root_value);

        free(data->data);
        free(data);
        nc->user_data = NULL;
      }
      break;
  }
}

static void
logger(const char* tag, const char* message) {
   time_t now;
   time(&now);
   printf("%s [%s]: %s\n", ctime(&now), tag, message);
}

static void
ev_handler(struct mg_connection *nc, int ev, void *p) {
  if (ev == MG_EV_HTTP_REQUEST) {
    mg_serve_http(nc, p, s_http_server_opts);
  }
}

static void
signal_handler(int sig_num) {
  signal(sig_num, signal_handler);
  s_received_signal = sig_num;
}

int
main(void) {
  FILE *fp = fopen("labels.txt", "r");
  if (!fp) {
    fprintf(stderr, "cannot load labels.txt\n");
    return 1;
  }
  char buf[BUFSIZ];
  JSON_Value *labels_value = json_value_init_array();
  labels_array = json_value_get_array(labels_value);
  while (fgets(buf, BUFSIZ, fp)) {
    char *p = strpbrk(buf, "\r\n");
    if (p) *p = 0;
    json_array_append_value(labels_array, json_value_init_string(buf));
  }
  fclose(fp);

  struct mg_mgr mgr;
  struct mg_connection *c;
  initialize_detect("mobilenet_quant_v1_224.tflite");

  mg_mgr_init(&mgr, NULL);
  c = mg_bind(&mgr, s_http_port, ev_handler);
  if (c == NULL) {
    fprintf(stderr, "Cannot start server on port %s\n", s_http_port);
    exit(EXIT_FAILURE);
  }

  s_http_server_opts.document_root = "assets";  // Serve current directory
  s_http_server_opts.enable_directory_listing = "yes";
  mg_register_http_endpoint(c, "/upload", handle_upload MG_UD_ARG(NULL));
  mg_set_protocol_http_websocket(c);

  signal(SIGINT, signal_handler);

  printf("Starting web server on port %s\n", s_http_port);
  while (!s_received_signal) {
    mg_mgr_poll(&mgr, 100);
  }
  mg_mgr_free(&mgr);

  json_value_free(labels_value);

  return 0;
}

/* vim:set et: */
