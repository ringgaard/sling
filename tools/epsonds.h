// Copyright 2023 Ringgaard Research ApS
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef EPSONDS_H_
#define EPSONDS_H_

// Scan depth.
#define COLOR_MONO          0
#define COLOR_GRAY          1
#define COLOR_RGB           2

// Orientation.
#define PORTRAIT            0
#define LANDSCAPE           1

// Paper size
#define A4                  0
#define A5                  1

// Scanner settings.
extern int scan_dpi;
extern int scan_color;
extern int orientation;
extern int paper_size;
extern int duplex;

int scanner_connect();
int scanner_disconnect();
int scanner_connected();

int scanner_lock();
int scanner_unlock();
int scanner_handshake();
int scanner_para();
int scanner_srdy();
int scan_document(const char *page_file_mask, int serial);

#endif  // EPSONDS_H_

