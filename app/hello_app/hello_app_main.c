/****************************************************************************
# apps/examples/httpsd/httpsd_main.c
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <strings.h>

#include "mbedtls/build_info.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/error.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_EXAMPLES_HTTPSD_PORT
#  define CONFIG_EXAMPLES_HTTPSD_PORT 443
#endif

/* Default Embedded Self-Signed Certificate and Key for 10.0.0.2 */

static const char g_default_srv_crt[] =
  "-----BEGIN CERTIFICATE-----\r\n"
  "MIIBizCCATKgAwIBAgIUGYOnqrNwXFUcpdV3mEDi78BjvGQwCgYIKoZIzj0EAwIw\r\n"
  "EzERMA8GA1UEAwwIMTAuMC4wLjIwHhcNMjYwOTIwMDM1NjE0WhcNMzYwOTE3MDM1\r\n"
  "NjE0WjATMREwDwYDVQQDDAgxMC4wLjAuMjBZMBMGByqGSM49AgEGCCqGSM49AwEH\r\n"
  "A0IABCTaoRPd89upDZ/s+3lQc3X5k4uxPuSN2v3nuVHBp3HaX19iFcN4p5xUQbIr\r\n"
  "7ncE+UOmXuGa0m4DTU1CbRzX5TKjZDBiMB0GA1UdDgQWBBSn6LHWtwphtyj3Z8vb\r\n"
  "2F6uCLPiojAfBgNVHSMEGDAWgBSn6LHWtwphtyj3Z8vb2F6uCLPiojAPBgNVHRMB\r\n"
  "Af8EBTADAQH/MA8GA1UdEQQIMAaHBAoAAAIwCgYIKoZIzj0EAwIDRwAwRAIgZvVJ\r\n"
  "Q1bTqYTVVItKJR+NBPFD9tbcdrHXAUxv0gsphcYCIEVMxJ5Ik/5MOXcNJ79pKeTt\r\n"
  "rHLBFi2CC5ZHyBkA6WMW\r\n"
  "-----END CERTIFICATE-----\r\n";

static const char g_default_srv_key[] =
  "-----BEGIN EC PRIVATE KEY-----\r\n"
  "MHcCAQEEICNURhoElf3yXH1+Ihnx+oWMylm2oS+8HMxe7mULCEmnoAoGCCqGSM49\r\n"
  "AwEHoUQDQgAEJNqhE93z26kNn+z7eVBzdfmTi7E+5I3a/ee5UcGncdpfX2IVw3in\r\n"
  "nFRBsivudwT5Q6Ze4ZrSbgNNTUJtHNflMg==\r\n"
  "-----END EC PRIVATE KEY-----\r\n";

static const char g_http_response[] =
  "HTTP/1.1 200 OK\r\n"
  "Server: NuttX-HTTPS/1.0\r\n"
  "Content-Type: text/html; charset=utf-8\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<!DOCTYPE html>\r\n"
  "<html>\r\n"
  "<head>\r\n"
  "  <meta charset=\"utf-8\">\r\n"
  "  <title>Allwinner F1C100s HTTPS Server</title>\r\n"
  "  <style>\r\n"
  "    body { font-family: system-ui, -apple-system, sans-serif; background: #0f172a; color: #f8fafc; padding: 40px; margin: 0; }\r\n"
  "    .card { background: #1e293b; border-radius: 12px; padding: 32px; max-width: 640px; margin: 40px auto; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.5); border: 1px solid #334155; }\r\n"
  "    h1 { color: #38bdf8; font-size: 24px; margin-top: 12px; margin-bottom: 8px; }\r\n"
  "    .badge { display: inline-block; background: #059669; color: #fff; padding: 4px 12px; border-radius: 9999px; font-size: 13px; font-weight: 600; }\r\n"
  "    p.desc { color: #94a3b8; font-size: 15px; margin-bottom: 24px; }\r\n"
  "    ul { list-style: none; padding: 0; margin: 0; border-top: 1px solid #334155; }\r\n"
  "    li { padding: 12px 0; border-bottom: 1px solid #334155; display: flex; justify-content: space-between; font-size: 14px; }\r\n"
  "    li strong { color: #94a3b8; font-weight: 500; }\r\n"
  "    li span { color: #e2e8f0; font-family: monospace; }\r\n"
  "  </style>\r\n"
  "</head>\r\n"
  "<body>\r\n"
  "  <div class=\"card\">\r\n"
  "    <span class=\"badge\">TLS / HTTPS Secure</span>\r\n"
  "    <h1>Allwinner F1C100s HTTPS Server</h1>\r\n"
  "    <p class=\"desc\">This connection is fully encrypted via TLS on Apache NuttX RTOS.</p>\r\n"
  "    <ul>\r\n"
  "      <li><strong>Target SoC</strong><span>Allwinner F1C100s (ARM926EJ-S @ 408MHz)</span></li>\r\n"
  "      <li><strong>SDIO Storage</strong><span>/mnt (FAT32, 12MHz High-Speed DMA)</span></li>\r\n"
  "      <li><strong>TLS Crypto Engine</strong><span>Mbed TLS 3.4.0</span></li>\r\n"
  "      <li><strong>Network Interface</strong><span>USB RNDIS (10.0.0.2:443)</span></li>\r\n"
  "    </ul>\r\n"
  "  </div>\r\n"
  "</body>\r\n"
  "</html>\r\n";

/****************************************************************************
 * Static File Serving Helpers
 ****************************************************************************/

static const char *get_mime_type(const char *path)
{
  const char *dot = strrchr(path, '.');
  if (!dot)
    {
      return "application/octet-stream";
    }

  dot++;
  if (strcasecmp(dot, "html") == 0 || strcasecmp(dot, "htm") == 0)
    {
      return "text/html; charset=utf-8";
    }

  if (strcasecmp(dot, "js") == 0 || strcasecmp(dot, "mjs") == 0)
    {
      return "text/javascript; charset=utf-8";
    }

  if (strcasecmp(dot, "css") == 0)
    {
      return "text/css; charset=utf-8";
    }

  if (strcasecmp(dot, "json") == 0 || strcasecmp(dot, "map") == 0)
    {
      return "application/json; charset=utf-8";
    }

  if (strcasecmp(dot, "wasm") == 0)
    {
      return "application/wasm";
    }

  if (strcasecmp(dot, "gz") == 0)
    {
      return "application/gzip";
    }

  if (strcasecmp(dot, "png") == 0)
    {
      return "image/png";
    }

  if (strcasecmp(dot, "svg") == 0)
    {
      return "image/svg+xml";
    }

  if (strcasecmp(dot, "ico") == 0)
    {
      return "image/x-icon";
    }

  if (strcasecmp(dot, "bin") == 0 || strcasecmp(dot, "iso") == 0)
    {
      return "application/octet-stream";
    }

  if (strcasecmp(dot, "txt") == 0)
    {
      return "text/plain; charset=utf-8";
    }

  return "application/octet-stream";
}

static void url_decode(char *dst, const char *src, size_t max_len)
{
  size_t i = 0;
  while (*src && i + 1 < max_len)
    {
      if (*src == '%' && isxdigit((int)src[1]) && isxdigit((int)src[2]))
        {
          char hex[3] = {src[1], src[2], 0};
          dst[i++] = (char)strtol(hex, NULL, 16);
          src += 3;
          continue;
        }

      dst[i++] = *src++;
    }

  dst[i] = '\0';
}

static void httpsd_serve_file(mbedtls_ssl_context *ssl, char *req_buf, int req_len)
{
  char method[8] = {0};
  char raw_url[256] = {0};
  char clean_url[256] = {0};
  char full_path[320] = {0};
  char hdr_buf[512];
  char file_buf[2048] aligned_data(32);
  struct stat st;
  long range_start = -1;
  long range_end = -1;
  bool is_head = false;
  int fd;

  /* Parse request line: GET /path HTTP/1.x */

  if (sscanf(req_buf, "%7s %255s", method, raw_url) != 2)
    {
      return;
    }

  if (strcmp(method, "HEAD") == 0)
    {
      is_head = true;
    }
  else if (strcmp(method, "GET") != 0)
    {
      snprintf(hdr_buf, sizeof(hdr_buf),
               "HTTP/1.1 501 Not Implemented\r\n"
               "Connection: close\r\n"
               "Content-Length: 0\r\n\r\n");
      mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf, strlen(hdr_buf));
      return;
    }

  /* Strip query parameters (?v=...) */

  char *query = strchr(raw_url, '?');
  if (query)
    {
      *query = '\0';
    }

  url_decode(clean_url, raw_url, sizeof(clean_url));

  /* Path traversal security check */

  if (strstr(clean_url, ".."))
    {
      snprintf(hdr_buf, sizeof(hdr_buf),
               "HTTP/1.1 403 Forbidden\r\n"
               "Connection: close\r\n"
               "Content-Length: 0\r\n\r\n");
      mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf, strlen(hdr_buf));
      return;
    }

  /* Default document index.html */

  if (strcmp(clean_url, "/") == 0 || clean_url[strlen(clean_url) - 1] == '/')
    {
      strlcat(clean_url, "index.html", sizeof(clean_url));
    }

  /* Construct full path under /mnt */

  snprintf(full_path, sizeof(full_path), "/mnt%s", clean_url);

  /* Check file existence */

  if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
      /* Check SPA fallback: if requested asset has no dot, fallback to /mnt/index.html */

      if (!strrchr(clean_url, '.') && stat("/mnt/index.html", &st) == 0 && S_ISREG(st.st_mode))
        {
          snprintf(full_path, sizeof(full_path), "/mnt/index.html");
        }
      else
        {
          /* If /mnt/index.html does not exist at all, send informative fallback page */

          if (strcmp(clean_url, "/index.html") == 0 && (stat("/mnt/index.html", &st) != 0 || !S_ISREG(st.st_mode)))
            {
              mbedtls_ssl_write(ssl, (const unsigned char *)g_http_response,
                                sizeof(g_http_response) - 1);
              return;
            }

          snprintf(hdr_buf, sizeof(hdr_buf),
                   "HTTP/1.1 404 Not Found\r\n"
                   "Content-Type: text/plain; charset=utf-8\r\n"
                   "Connection: close\r\n"
                   "Content-Length: 15\r\n\r\n"
                   "Asset not found");
          mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf, strlen(hdr_buf));
          return;
        }
    }

  /* Parse Range header: Range: bytes=100-200 or Range: bytes=100- */

  char *range_hdr = strstr(req_buf, "Range: bytes=");
  if (!range_hdr)
    {
      range_hdr = strstr(req_buf, "range: bytes=");
    }

  if (range_hdr)
    {
      range_hdr += 13;
      char *dash = strchr(range_hdr, '-');
      if (dash)
        {
          range_start = atol(range_hdr);
          if (dash[1] >= '0' && dash[1] <= '9')
            {
              range_end = atol(dash + 1);
            }
          else
            {
              range_end = (long)st.st_size - 1;
            }
        }
    }

  const char *mime = get_mime_type(full_path);
  long content_len;
  int hdr_len;

  if (range_start >= 0 && range_end >= range_start && range_start < (long)st.st_size)
    {
      if (range_end >= (long)st.st_size)
        {
          range_end = (long)st.st_size - 1;
        }

      content_len = range_end - range_start + 1;
      hdr_len = snprintf(hdr_buf, sizeof(hdr_buf),
                         "HTTP/1.1 206 Partial Content\r\n"
                         "Server: F1C100s-HTTPS/1.0\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %ld\r\n"
                         "Content-Range: bytes %ld-%ld/%ld\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "Connection: close\r\n"
                         "Cross-Origin-Opener-Policy: same-origin\r\n"
                         "Cross-Origin-Embedder-Policy: require-corp\r\n"
                         "Cross-Origin-Resource-Policy: same-origin\r\n"
                         "\r\n",
                         mime, content_len, range_start, range_end, (long)st.st_size);
    }
  else
    {
      range_start = 0;
      content_len = (long)st.st_size;
      hdr_len = snprintf(hdr_buf, sizeof(hdr_buf),
                         "HTTP/1.1 200 OK\r\n"
                         "Server: F1C100s-HTTPS/1.0\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %ld\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "Connection: close\r\n"
                         "Cross-Origin-Opener-Policy: same-origin\r\n"
                         "Cross-Origin-Embedder-Policy: require-corp\r\n"
                         "Cross-Origin-Resource-Policy: same-origin\r\n"
                         "\r\n",
                         mime, content_len);
    }

  /* Open file and prepare streaming before sending headers */

  fd = -1;
  if (!is_head)
    {
      fd = open(full_path, O_RDONLY);
      if (fd < 0)
        {
          snprintf(hdr_buf, sizeof(hdr_buf),
                   "HTTP/1.1 404 Not Found\r\n"
                   "Content-Type: text/plain; charset=utf-8\r\n"
                   "Connection: close\r\n"
                   "Content-Length: 15\r\n\r\n"
                   "File open error");
          mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf, strlen(hdr_buf));
          return;
        }

      if (range_start > 0)
        {
          if (lseek(fd, range_start, SEEK_SET) < 0)
            {
              close(fd);
              snprintf(hdr_buf, sizeof(hdr_buf),
                       "HTTP/1.1 416 Range Not Satisfiable\r\n"
                       "Connection: close\r\n"
                       "Content-Length: 0\r\n\r\n");
              mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf, strlen(hdr_buf));
              return;
            }
        }
    }

  /* Send HTTP response headers */

  int hdr_written = 0;
  while (hdr_written < hdr_len)
    {
      int ret = mbedtls_ssl_write(ssl, (const unsigned char *)hdr_buf + hdr_written,
                                  hdr_len - hdr_written);
      if (ret <= 0)
        {
          if (ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_WANT_READ)
            {
              usleep(1000);
              continue;
            }

          if (fd >= 0)
            {
              close(fd);
            }
          return;
        }

      hdr_written += ret;
    }

  if (is_head)
    {
      return;
    }

  /* Stream file in chunks */

  long remaining = content_len;
  while (remaining > 0)
    {
      size_t chunk = (remaining > sizeof(file_buf)) ? sizeof(file_buf) : (size_t)remaining;
      ssize_t nread = read(fd, file_buf, chunk);
      if (nread <= 0)
        {
          break;
        }

      int written = 0;
      while (written < nread)
        {
          int wret = mbedtls_ssl_write(ssl, (const unsigned char *)file_buf + written, nread - written);
          if (wret <= 0)
            {
              if (wret == MBEDTLS_ERR_SSL_WANT_WRITE || wret == MBEDTLS_ERR_SSL_WANT_READ)
                {
                  usleep(1000);
                  continue;
                }

              /* Connection broken or TLS error */

              close(fd);
              return;
            }

          written += wret;
        }

      remaining -= nread;
    }

  close(fd);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int ret;
  int port = CONFIG_EXAMPLES_HTTPSD_PORT;
  char port_str[16];
  mbedtls_net_context listen_fd;
  mbedtls_net_context client_fd;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_x509_crt srvcert;
  mbedtls_pk_context pkey;
  unsigned char buf[1024];
  const char *pers = "httpsd_f1c100s";
  struct stat st;
  bool use_sd_cert = false;

  if (argc > 1)
    {
      port = atoi(argv[1]);
      if (port <= 0 || port > 65535)
        {
          port = CONFIG_EXAMPLES_HTTPSD_PORT;
        }
    }

  snprintf(port_str, sizeof(port_str), "%d", port);
  printf("[httpsd] Starting HTTPS Server on port %s...\n", port_str);

  mbedtls_net_init(&listen_fd);
  mbedtls_net_init(&client_fd);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&srvcert);
  mbedtls_pk_init(&pkey);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  /* 1. Seed RNG */

  ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers));
  if (ret != 0)
    {
      printf("[httpsd] ERROR: ctr_drbg_seed failed: -0x%04x\n", -ret);
      goto exit;
    }

  /* 2. Check for custom certificate on SD card (/mnt/server.crt & server.key) */

  if (stat("/mnt/server.crt", &st) == 0 && stat("/mnt/server.key", &st) == 0)
    {
      printf("[httpsd] Loading certificate from SD card (/mnt/server.crt)...\n");
      ret = mbedtls_x509_crt_parse_file(&srvcert, "/mnt/server.crt");
      if (ret == 0)
        {
          ret = mbedtls_pk_parse_keyfile(&pkey, "/mnt/server.key", NULL,
                                         mbedtls_ctr_drbg_random, &ctr_drbg);
          if (ret == 0)
            {
              use_sd_cert = true;
              printf("[httpsd] Loaded custom SD card certificate successfully!\n");
            }
          else
            {
              printf("[httpsd] Failed to parse /mnt/server.key: -0x%04x, falling back to embedded\n", -ret);
            }
        }
      else
        {
          printf("[httpsd] Failed to parse /mnt/server.crt: -0x%04x, falling back to embedded\n", -ret);
        }
    }

  if (!use_sd_cert)
    {
      printf("[httpsd] Loading embedded default certificate...\n");
      ret = mbedtls_x509_crt_parse(&srvcert,
                                   (const unsigned char *)g_default_srv_crt,
                                   sizeof(g_default_srv_crt));
      if (ret != 0)
        {
          printf("[httpsd] ERROR: x509_crt_parse failed: -0x%04x\n", -ret);
          goto exit;
        }

      ret = mbedtls_pk_parse_key(&pkey,
                                 (const unsigned char *)g_default_srv_key,
                                 sizeof(g_default_srv_key),
                                 NULL, 0,
                                 mbedtls_ctr_drbg_random, &ctr_drbg);
      if (ret != 0)
        {
          printf("[httpsd] ERROR: pk_parse_key failed: -0x%04x\n", -ret);
          goto exit;
        }
    }

  /* 3. Bind TCP listening socket */

  ret = mbedtls_net_bind(&listen_fd, NULL, port_str, MBEDTLS_NET_PROTO_TCP);
  if (ret != 0)
    {
      printf("[httpsd] ERROR: net_bind failed: -0x%04x\n", -ret);
      goto exit;
    }

  /* 4. Setup SSL configuration */

  ret = mbedtls_ssl_config_defaults(&conf,
                                    MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      printf("[httpsd] ERROR: ssl_config_defaults failed: -0x%04x\n", -ret);
      goto exit;
    }

  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
  mbedtls_ssl_conf_ca_chain(&conf, srvcert.next, NULL);

  ret = mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);
  if (ret != 0)
    {
      printf("[httpsd] ERROR: ssl_conf_own_cert failed: -0x%04x\n", -ret);
      goto exit;
    }

  ret = mbedtls_ssl_setup(&ssl, &conf);
  if (ret != 0)
    {
      printf("[httpsd] ERROR: ssl_setup failed: -0x%04x\n", -ret);
      goto exit;
    }

  printf("[httpsd] HTTPS Server listening on https://10.0.0.2:%s/\n", port_str);

  /* Main connection loop */

  for (;;)
    {
      mbedtls_net_free(&client_fd);
      mbedtls_ssl_session_reset(&ssl);

      ret = mbedtls_net_accept(&listen_fd, &client_fd, NULL, 0, NULL);
      if (ret != 0)
        {
          if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
              printf("[httpsd] net_accept failed: -0x%04x\n", -ret);
            }
          continue;
        }

      /* Set 8-second socket timeout to prevent slowloris / stuck sockets */

      struct timeval tv;
      tv.tv_sec = 8;
      tv.tv_usec = 0;
      setsockopt(client_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      setsockopt(client_fd.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

      mbedtls_ssl_set_bio(&ssl, &client_fd,
                          mbedtls_net_send, mbedtls_net_recv, NULL);

      /* TLS Handshake */

      while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
        {
          if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
              ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
              break;
            }
        }

      if (ret != 0)
        {
          continue;
        }

      /* Read HTTP Request */

      memset(buf, 0, sizeof(buf));
      ret = mbedtls_ssl_read(&ssl, buf, sizeof(buf) - 1);
      if (ret > 0)
        {
          /* Serve static file from /mnt */

          httpsd_serve_file(&ssl, (char *)buf, ret);
        }

      /* Close session notify */

      mbedtls_ssl_close_notify(&ssl);
    }

exit:
  mbedtls_net_free(&client_fd);
  mbedtls_net_free(&listen_fd);
  mbedtls_x509_crt_free(&srvcert);
  mbedtls_pk_free(&pkey);
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);

  return ret;
}
