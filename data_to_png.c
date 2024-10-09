#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 3840  // Larghezza in pixel di un PNG 4K
#define HEIGHT 2160 // Altezza in pixel di un PNG 4K
#define MAX_PIXELS (WIDTH * HEIGHT)
#define BYTES_PER_PIXEL 4

png_bytep *row_pointers = NULL;

// Funzione per creare il PNG e scrivere i dati del file nei pixel
void create_png_with_data(char *output_filename, unsigned char *data,
                          size_t data_size) {
  // Alloca la memoria per le righe dell'immagine
  row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * HEIGHT);
  for (int y = 0; y < HEIGHT; y++) {
    row_pointers[y] = (png_byte *)malloc(WIDTH * BYTES_PER_PIXEL);
  }

  // Scrive i byte del file nei primi pixel
  size_t byte_index = 0;
  int end_col = 0, end_row = 0, end_byte_pos = 0;

  for (int y = 0; y < HEIGHT; y++) {
    png_bytep row = row_pointers[y];
    for (int x = 0; x < WIDTH; x++) {
      png_bytep px = &(row[x * BYTES_PER_PIXEL]);
      if (byte_index < data_size) {
        // Scrive i dati del file nei 4 canali (RGBA)
        px[0] = (byte_index < data_size) ? data[byte_index] : 0; // Rosso
        px[1] =
            (byte_index + 1 < data_size) ? data[byte_index + 1] : 0; // Verde
        px[2] = (byte_index + 2 < data_size) ? data[byte_index + 2] : 0; // Blu
        px[3] =
            (byte_index + 3 < data_size) ? data[byte_index + 3] : 0; // Alpha

        byte_index += 4;
        if (byte_index >= data_size) {
          // Memorizza la posizione dove finisce il file
          end_col = ++x;
          end_row = y;
          end_byte_pos = data_size % 4; // Indica in quale canale termina
        }
      } else {
        // Pixel di riempimento
        px[0] = 0; // Rosso
        px[1] = 0; // Verde
        px[2] = 0; // Blu
        px[3] = 0; // Alpha
      }
    }
  }

  // Scrivi le informazioni di fine file nei primi 4 byte
  png_bytep first_row = row_pointers[0];

  // Byte 0-2: Memorizza la colonna e riga (24 bit: 12 bit per colonna, 12 per
  // riga)
  first_row[0] = (end_row >> 4) & 0xFF; // Riga (alti 8 bit)
  first_row[1] =
      ((end_row & 0xF) << 4) |
      ((end_col >> 8) & 0xF);    // Riga (bassi 4 bit) + Colonna (alti 4 bit)
  first_row[2] = end_col & 0xFF; // Colonna (bassi 8 bit)

  // Byte 3: Canale di fine file (2 bit) e offset (6 bit)
  first_row[3] = ((end_byte_pos & 0x3) << 6) |
                 (0x3F & 0); // Byte di offset (puoi gestirlo se necessario)

  // Crea il file PNG
  FILE *fp = fopen(output_filename, "wb");
  if (!fp)
    abort();

  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    abort();

  png_infop info = png_create_info_struct(png);
  if (!info)
    abort();

  if (setjmp(png_jmpbuf(png)))
    abort();

  png_init_io(png, fp);

  // Imposta le informazioni dell'immagine (larghezza, altezza, formato RGBA)
  png_set_IHDR(png, info, WIDTH, HEIGHT, 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  // Scrive i dati dell'immagine
  png_write_image(png, row_pointers);
  png_write_end(png, NULL);

  // Libera la memoria
  for (int y = 0; y < HEIGHT; y++) {
    free(row_pointers[y]);
  }
  free(row_pointers);

  fclose(fp);
  png_destroy_write_struct(&png, &info);
}

// Funzione per caricare un file in memoria
unsigned char *load_file(char *filename, size_t *out_size) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("Errore nell'apertura del file");
    exit(EXIT_FAILURE);
  }

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  rewind(file);

  unsigned char *buffer = (unsigned char *)malloc(file_size);
  if (!buffer) {
    perror("Errore nell'allocazione del buffer");
    fclose(file);
    exit(EXIT_FAILURE);
  }

  fread(buffer, 1, file_size, file);
  fclose(file);

  *out_size = file_size;
  return buffer;
}

// Funzione principale
int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("Usage: %s <input file> <output PNG>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // Carica il file in memoria
  size_t file_size;
  unsigned char *file_data = load_file(argv[1], &file_size);

  // Verifica che il file non sia più grande di un PNG 4K
  if (file_size > (MAX_PIXELS * 4)) {
    printf("Il file è troppo grande per essere contenuto in un PNG 4K.\n");
    free(file_data);
    exit(EXIT_FAILURE);
  }

  // Crea il PNG con i dati del file
  create_png_with_data(argv[2], file_data, file_size);

  free(file_data);
  return 0;
}
