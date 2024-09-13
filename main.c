/* Per compilare aggiungere "-lpng" su linux
 * Su MacOS bisogna dire dove si trovano gli header e le librerie, con
 * l'installazione delle librerie tramite homebrew quindi il comando diventa
 * così "clang example_libpng.c -o example -I/opt/homebrew/include
 * -L/opt/homebrew/lib -lpng -lz -lc"
 */

/* Utilizzo i primi 4 byte di un immagine per definire in maniera precisa quando
 * un file termina e quindi dopo iniziano i pixel di riempimento.
 * Utilizzo poi 12 bits per la riga e i succesivi 12 bits per la colonna, in
 * totale fanno 24 bits => 3 byte. Del byte sucessivo poi utilizzavo 2 bits per
 * indicare in quale byte di un pixel termina il file:
 *
 * primo bit      =   R
 * secondo bit    =   G
 * terzo bit      =   B
 * quarto bit     =   A
 *
 * Memorizzo l'estensione del file che salvo, utilizzo i restanti 6 bit che
 * avanzano per salvare la lunghezza in decimale dell'estensione del file
 * questo comporta che si può avere massimo 64 caratteri per l'estensione del
 * file che ritengo abbondanti.
 * Utilizzo poi 8 byte per il numero di chunks che ci sono e ulteriori 8 per
 * definire il chunk finale.
 * Dopodichè utilizzo un numero variabile di byte per contenere i caratteri
 * dell'estensione del file che sto trasformando.
 */

#include <errno.h> // Include per la gestione degli errori (definisce variabili per gli errori di sistema come 'errno')
#include <fcntl.h> // Include per la gestione dei file (fornisce funzioni come open(), read(), write(), etc.)
#include <ftw.h> // Include per funzioni che permettono di eseguire operazioni su file e directory come ftw() (file tree walk)
#include <math.h> // Include per funzioni matematiche come pow(), sqrt(), sin(), cos(), etc.
#include <png.h> // Include per usare le funzioni della libreria libpng, utilizzata per la lettura e scrittura di file PNG
#include <stdint.h> // Include per tipi di dati con dimensioni fisse (es. int8_t, uint16_t, etc.), utile per compatibilità a basso livello
#include <stdio.h> // Include per funzioni di input/output come fopen(), fclose(), printf(), etc.
#include <stdlib.h> // Include per funzioni di allocazione dinamica (malloc(), free()) e altre utility come exit()
#include <string.h> // Include per funzioni di manipolazione delle stringhe come strlen(), strcpy(), memcmp(), etc.
#include <unistd.h> // Include per funzioni di sistema POSIX come fork(), exec(), sleep(), close(), etc., comuni nei sistemi UNIX-like

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500L

#define ERROR_PNG_STRUCT_WRITE_CREATION 2
#define ERROR_PNG_INFO_STRUCT_CREATION 3
#define ERROR_PNG_WRITE_ELABORATION 4
#define ERROR_ROWS_NOT_ALLOCATED 5

// Risoluzione di default = 4K (Ultra HD) -> 33 177 600 bytes, 265 420 800 bits
#define WIDTH_DEFAULT 3840
#define HEIGHT_DEFAULT 2160
#define BYTES_PER_PIXEL 4
#define BYTES_PER_ROW (WIDTH_DEFAULT * BYTES_PER_PIXEL)
#define BYTES_PER_ROW (WIDTH_DEFAULT * BYTES_PER_PIXEL)
#define PNG_TOTAL_PIXELS (WIDTH_DEFAULT * HEIGHT_DEFAULT)
#define PNG_TOTAL_BYTES (PNG_TOTAL_PIXELS * BYTES_PER_PIXEL)
#define BUFFER_SIZE 4096
#define EXTENSION_MAX_LENGTH 64 // l'ultimo carattere è quello nullo '\0'
#define HEADER_INFO_LENGTH 20

#define BYTES_INSIDE_INT64 8
#define BYTES_INSIDE_INT32 4
#define BYTES_INSIDE_INT16 2

#define TRUE 1
#define FALSE 0

struct Pixel {
  uint8_t r, g, b, a;
} typedef pixel_t;

struct HEADER_INFO {
  uint64_t total_frames, last_frame;
  // valore dell'ultima riga, ultima colonna, ultimo canale e lunghezza
  // dell'estensione formattate
  uint32_t data_formatted;
  // sono solo per salvare i dati, non sono formattati
  uint16_t last_byte_row, last_byte_column;
  uint8_t last_channel_and_extension_length;
} typedef header_info_t;

// Variabili globali
const int width = WIDTH_DEFAULT;
const int height = HEIGHT_DEFAULT;
png_bytep image_data = NULL;
png_byte *extension_name = NULL; // Puntatore per i caratteri dell'estensione
header_info_t header_info;

// Call-back to the 'remove()' function called by nftw()
static int remove_callback(const char *pathname,
                           __attribute__((unused)) const struct stat *sbuf,
                           __attribute__((unused)) int type,
                           __attribute__((unused)) struct FTW *ftwb) {
  return remove(pathname);
}

// Prende un numero intero senza segno di 8 bit e lo converte in una stringa che
// rappresenta il numero in formato binario.
char *uint8_t_to_binary_string(uint8_t value) {
  // Alloca dinamicamente memoria per la stringa binaria.
  // 8 caratteri per i bit + 1 per il terminatore di stringa ('\0').
  char *binary_str = (char *)malloc(sizeof(char) * 8 + 1);
  // Inizializza un indice per tenere traccia della posizione nella stringa.
  uint8_t index = 0;

  // Cicla attraverso ciascun bit di 'value', partendo dal bit più
  // significativo. 'i' viene inizializzato a 128 (10000000 in binario), che
  // rappresenta il bit più a sinistra. In ogni iterazione, 'i' viene diviso per
  // 2 (>>= 1), spostando il bit 1 verso destra.
  for (uint8_t i = 128; i != 0; i >>= 1) {
    // L'operazione 'value & i' esegue una AND bit a bit tra 'value' e 'i'.
    // Se il bit corrente (in 'i') è settato in 'value', allora l'espressione
    // restituisce 1, altrimenti inserisce '0'.
    binary_str[index] = (value & i) ? '1' : '0';
    index++;
  }

  // Aggiunge il carattere nullo alla fine per indicare il termine.
  binary_str[index] = '\0';
  return binary_str;
}

uint32_t calculate_offset(const uint16_t row, const uint16_t column) {
  return row * width + column;
}

uint8_t *split_uint64_t_into_bytes(const uint64_t value) {
  uint8_t *bytes = (uint8_t *)malloc(sizeof(uint8_t) * BYTES_INSIDE_INT64);
  bytes[7] = value & 0xff;
  bytes[6] = (value >> 8) & 0xff;
  bytes[5] = (value >> 16) & 0xff;
  bytes[4] = (value >> 24) & 0xff;
  bytes[3] = (value >> 32) & 0xff;
  bytes[2] = (value >> 40) & 0xff;
  bytes[1] = (value >> 48) & 0xff;
  bytes[0] = (value >> 56) & 0xff;

  return bytes;
}

uint8_t *split_uint32_t_into_bytes(const uint32_t value) {
  uint8_t *bytes = (uint8_t *)malloc(sizeof(uint8_t) * BYTES_INSIDE_INT32);
  bytes[3] = value & 0xff;
  bytes[2] = (value >> 8) & 0xff;
  bytes[1] = (value >> 16) & 0xff;
  bytes[0] = (value >> 24) & 0xff;

  return bytes;
}

uint8_t *split_uint16_t_into_bytes(const uint16_t value) {
  uint8_t *bytes = (uint8_t *)malloc(sizeof(uint8_t) * BYTES_INSIDE_INT16);
  bytes[1] = value & 0xff;
  bytes[0] = (value >> 8) & 0xff;

  return bytes;
}

long get_file_size(FILE *fp) {
  fseek(fp, 0, SEEK_END); // seek to end of file
  fflush(fp);
  const long size = ftell(fp); // get current file pointer
  fseek(fp, 0, SEEK_SET);      // seek back to beginning of file
  return size;
}

// Calcola la lunghezza dell'estensione di un file a partire dal nome del file
// stesso.
uint8_t get_extension_length(const char *filename) {
  // cerca l'ultima occorrenza di un carattere (in questo caso, il punto .) in
  // una stringa.
  char *dot = strrchr(filename, '.');
  if (dot == NULL) {
    return 0; // significa che il file non ha estensione
  }

  // Calcola la lunghezza della parte della stringa che va dal punto '.' fino
  // alla fine del nome del file, escludendo il punto, visto che tolgo 1 dal
  // totale e controllo che la lunghezza dell'estensione non superi il massimo
  // che ho stabilito
  const uint8_t ext_size = ((strlen(dot) - 1) > EXTENSION_MAX_LENGTH)
                               ? EXTENSION_MAX_LENGTH
                               : strlen(dot) - 1;

  dot = NULL;
  return ext_size;
}

char *get_extension_string(const char *filename) {
  // Ottiene la lunghezza dell'estensione
  const uint8_t ext_size = get_extension_length(filename);

  // Se l'estensione è lunga 0, significa che non esiste un'estensione
  if (ext_size == 0)
    return NULL;

  // Usa la funzione strrchr per trovare l'ultima occorrenza del carattere '.'
  // nel nome del file. La funzione restituisce un puntatore alla posizione del
  // '.'. Trovato il '.', aggiungiamo 1 al puntatore per ottenere un puntatore
  // al primo carattere dell'estensione (cioè dopo il '.').
  char *dot = strrchr(filename, '.') + 1;

  // Alloca memoria per la stringa dell'estensione, includendo un byte
  // aggiuntivo per il terminatore di stringa ('\0').
  char *ext_string = (char *)malloc(ext_size + 1);

  // Copia i caratteri che formano l'estensione dalla stringa 'dot' (il
  // puntatore che punta alla parte dell'estensione) nella stringa allocata
  // 'ext_string'. Si copia fino a 'ext_size' caratteri.
  strncpy(ext_string, dot, ext_size);

  // Aggiungo il carattere nullo ('\0') alla fine della stringa per terminare
  // correttamente la stringa.
  ext_string[ext_size] = '\0';
  return ext_string;
}

uint8_t *read_buffered_file(FILE *fp, uint16_t *byte_to_reads) {
  uint8_t *buffer = (uint8_t *)malloc(*byte_to_reads);
  const unsigned long byte_reads = fread(buffer, 1, *byte_to_reads, fp);
  return buffer;
}

// Allora in teoria ora sembra funzionare, ma va rivisitato perchè ho
// "sistemato" il tutto togliendo 1 all'ultima riga e colonna, ma non penso che
// sia il modo corretto
header_info_t predict_last_data_position(const long file_size_with_header,
                                         const uint8_t extension_length) {
  header_info_t info;

  // Numero di chunk completi
  const uint64_t complete_chunks =
      floor((double)file_size_with_header / PNG_TOTAL_BYTES);
  // Numeri di bytes che contiene l'ultimo chunk
  const uint32_t bytes_last_chunk =
      (PNG_TOTAL_BYTES >= file_size_with_header)
          ? file_size_with_header
          : file_size_with_header - complete_chunks * PNG_TOTAL_BYTES;

  // Numero di righe complete dell'ultimo chunk
  const uint16_t complete_last_chunk_rows =
      floor((double)bytes_last_chunk / BYTES_PER_ROW);
  // Numero di bytes rimanenti null'ultima riga dell'ultimo chunk
  const uint16_t bytes_last_chunk_row =
      bytes_last_chunk - complete_last_chunk_rows * BYTES_PER_ROW;
  // Numeri di pixels completi nell'ultima riga del'ultimo chunk
  const uint16_t complete_last_row_pixels =
      floor((double)(BYTES_PER_ROW - bytes_last_chunk_row) / BYTES_PER_PIXEL);

  // potrebbe essere zero, se ho un numero giusto di bytes che riempono i pixels
  // (c'è che non ci sono pixels parzialmente valorizzati)
  const uint16_t remaining_bytes_last_row =
      BYTES_PER_ROW - complete_last_row_pixels * BYTES_PER_PIXEL;

  // indice dell'ultima riga
  const uint16_t last_row = height - (height - complete_last_chunk_rows);
  // indice dell'ultima colonna
  const uint16_t last_column =
      width - (width - complete_last_row_pixels - remaining_bytes_last_row);
  // indice dell'ultimo canale
  // 0 = alpha, 1 = red, 2 = green, 3 = blue
  const uint8_t last_channel = remaining_bytes_last_row;

  info.last_byte_column = last_column;
  info.last_byte_row = last_row;
  // posiziono i bit che rappresentano l'ultimo canale nei bit più
  // significativi, stabilito dalla formattazione
  info.last_channel_and_extension_length = last_channel << 6;
  info.last_channel_and_extension_length += extension_length;

  return info;
}

// Funzione per scrivere un file PNG
void write_png_file(char *filename) {
  FILE *fp = fopen(filename,
                   "wb"); // Apre il file per la scrittura in modalità binaria
  if (!fp)
    exit(EXIT_FAILURE);

  // Crea una struttura per scrivere il PNG
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    exit(ERROR_PNG_STRUCT_WRITE_CREATION);

  // Crea una struttura per le informazioni del PNG
  png_infop info = png_create_info_struct(png);
  if (!info)
    exit(ERROR_PNG_INFO_STRUCT_CREATION);

  // Imposta il salto in caso di errore
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    exit(ERROR_PNG_WRITE_ELABORATION);
  }

  // Inizializza l'output per scrivere nel file
  png_init_io(png, fp);

  // Imposta le informazioni dell'immagine di output (larghezza, altezza,
  // formato RGBA)
  png_set_IHDR(png, info, width, height,
               8,                            // 8 bit di profondità
               PNG_COLOR_TYPE_RGBA,          // Formato colore RGBA
               PNG_INTERLACE_NONE,           // Senza interlacciamento
               PNG_COMPRESSION_TYPE_DEFAULT, // Compressione di default
               PNG_FILTER_TYPE_DEFAULT       // Filtro di default
  );
  png_write_info(png, info); // Scrive le informazioni dell'immagine nel file

  // Controlla se l'immagine è stata allocata
  if (!image_data)
    exit(EXIT_FAILURE);

  // Crea un array di puntatori, uno per ogni riga
  png_bytep *row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * height);
  if (!row_pointers) {
    fclose(fp);
    exit(ERROR_ROWS_NOT_ALLOCATED);
  }

  // Imposta ciascun puntatore per puntare all'inizio di ogni riga in image_data
  for (int y = 0; y < height; y++)
    row_pointers[y] = &(image_data[calculate_offset(y, 0) * BYTES_PER_PIXEL]);

  // Scrive i dati dell'immagine
  png_write_image(png, row_pointers);
  png_write_end(png, NULL); // Termina la scrittura

  // Chiudo il file di output
  fclose(fp);

  // Libera le strutture allocate per la scrittura dell'immagine
  png_destroy_write_struct(&png, &info);
}

// da finire
void convert_file(FILE *fp, const char *filename,
                  const char *base_output_filename) {
  // Alloca un array unidimensionale per memorizzare tutti i bytes dell'immagine
  image_data = (png_bytep)malloc(width * height * BYTES_PER_PIXEL);

  // Nel primo frame i primi HEADER_INFO_LENGTH bytes sono occupati per l'header
  const long file_size = get_file_size(fp);
  const uint8_t ext_length = get_extension_length(filename);
  const uint64_t file_size_with_header =
      file_size + ext_length + HEADER_INFO_LENGTH;
  const uint64_t n_chunks =
      (file_size_with_header / PNG_TOTAL_BYTES) == 0
          ? 1
          : ceil((double)file_size_with_header / PNG_TOTAL_BYTES);

  header_info.total_frames = n_chunks;
  header_info.last_frame = n_chunks - 1;
  printf("Total frames: %llu\nLast frame index: %llu\n",
         header_info.total_frames, header_info.last_frame);
  printf("Dimensione del file = %lu bytes\n", get_file_size(fp));
  printf("Dimensione del file con info = %llu bytes\n", file_size_with_header);

  // valori default iniziali
  header_info.last_byte_column = 0;
  header_info.last_byte_row = 0;
  header_info.last_channel_and_extension_length = 0;
  pixel_t *pixels_buffered = NULL; // non serve più si può togliere

  uint64_t remaining_bytes = file_size;
  uint8_t is_last_frame = FALSE;
  uint32_t current_frame_bytes_to_read = 0;
  for (uint64_t chunk = 0; chunk < n_chunks; chunk++) {
    if (PNG_TOTAL_BYTES >= remaining_bytes) {
      is_last_frame = TRUE;
      current_frame_bytes_to_read = remaining_bytes;
    } else {
      current_frame_bytes_to_read = PNG_TOTAL_BYTES;
    }

    // Divido in bytes le informazioni dell'header, così le salvo sulla matrice
    // della prima immagine
    if (chunk == 0) {
      // Formatto in un uint32_t le informazioni inerenti l'ultima riga,
      // all'ultima colonna, ultimo canale e lunghezza dell'estensione
      uint32_t tmp = 0;
      const header_info_t predict_info =
          predict_last_data_position(file_size_with_header, ext_length);

      // prima salvo il valore della riga formattata
      printf("Last row: %u\n", predict_info.last_byte_row);
      tmp = predict_info.last_byte_row;
      tmp = tmp << 20;
      printf("Last row shifted: %u\n", tmp);
      header_info.data_formatted = tmp;

      // poi salvo il valore della colonna
      printf("Last column: %u\n", predict_info.last_byte_column);
      tmp = predict_info.last_byte_column;
      tmp = tmp << 8;
      printf("Last column shifted: %u\n", tmp);
      // aggiungo, perchè devo mantere i bit inerenti alla riga
      header_info.data_formatted += tmp;

      // aggiungo infine il canale e la lunghezza dell'estensione
      printf("Last channel & ext length: %u\n",
             predict_info.last_channel_and_extension_length);
      header_info.data_formatted +=
          predict_info.last_channel_and_extension_length;
      printf("All info together: %u\n", header_info.data_formatted);

      // separo in byte le informazioni dell'header
      const uint8_t *data_formatted_splitted =
          split_uint32_t_into_bytes(header_info.data_formatted);
      const uint8_t *total_frames_splitted =
          split_uint64_t_into_bytes(header_info.total_frames);
      const uint8_t *last_frame_splitted =
          split_uint64_t_into_bytes(header_info.last_frame);

      // Volendo posso mettere questi byte in un unico array e poi usare un loop
      // per inserirli nella matrice dell'immagine, però tanto vale scrivere
      // manualmente le assegnazioni
      uint8_t byte_index = 0;

      image_data[byte_index++] = data_formatted_splitted[0];
      image_data[byte_index++] = data_formatted_splitted[1];
      image_data[byte_index++] = data_formatted_splitted[2];
      image_data[byte_index++] = data_formatted_splitted[3];

      image_data[byte_index++] = total_frames_splitted[0];
      image_data[byte_index++] = total_frames_splitted[1];
      image_data[byte_index++] = total_frames_splitted[2];
      image_data[byte_index++] = total_frames_splitted[3];
      image_data[byte_index++] = total_frames_splitted[4];
      image_data[byte_index++] = total_frames_splitted[5];
      image_data[byte_index++] = total_frames_splitted[6];
      image_data[byte_index++] = total_frames_splitted[7];

      image_data[byte_index++] = last_frame_splitted[0];
      image_data[byte_index++] = last_frame_splitted[1];
      image_data[byte_index++] = last_frame_splitted[2];
      image_data[byte_index++] = last_frame_splitted[3];
      image_data[byte_index++] = last_frame_splitted[4];
      image_data[byte_index++] = last_frame_splitted[5];
      image_data[byte_index++] = last_frame_splitted[6];
      image_data[byte_index++] = last_frame_splitted[7];

      current_frame_bytes_to_read -= HEADER_INFO_LENGTH;

      char *ext_str = get_extension_string(filename);
      printf("Extension: %s\n", ext_str);
      printf("Extension Length: %u\n", ext_length);
      for (uint8_t i = 0; i < ext_length; i++)
        image_data[byte_index++] = ext_str[i];

      current_frame_bytes_to_read -= ext_length;

      data_formatted_splitted = NULL;
      total_frames_splitted = NULL;
      last_frame_splitted = NULL;

      /*for (int i = 0; i < 23; i++) {
        printf("[%5d]: %3u -> %s -> %02X\n", i, image_data[i],
               uint8_t_to_binary_string(image_data[i]), image_data[i]);
      }
      exit(EXIT_SUCCESS);*/
    }

    /*if (chunk != 0) {
      printf("Bytes rimanenti: %llu\n", remaining_bytes);
      exit(EXIT_SUCCESS);
    }*/

    // Numero di buffers necessari per leggere i rimanenti bytes
    uint16_t total_buffers =
        ceil((double)current_frame_bytes_to_read / BUFFER_SIZE);
    printf("Total buffers: %u\n", total_buffers);
    uint8_t *buffer = NULL;
    uint16_t byte_to_reads = 0;
    // punto al byte successivo a tutte le informazioni iniziali
    uint32_t byte_pointer = (chunk == 0) ? HEADER_INFO_LENGTH + ext_length : 0;
    for (uint16_t i = 0; i < total_buffers; i++) {
      // leggo al massimo 4096 byte, se ce ne sono meno leggo solo quelli che
      // rimangono
      if (BUFFER_SIZE > current_frame_bytes_to_read)
        byte_to_reads = current_frame_bytes_to_read;
      else
        byte_to_reads = BUFFER_SIZE;

      buffer = read_buffered_file(fp, &byte_to_reads);
      current_frame_bytes_to_read -= byte_to_reads;
      remaining_bytes -= byte_to_reads;
      // printf("Buffer numero %4d, bytes letti: %u\n", i, byte_to_reads);

      // salvo il buffer di dati che ho appena letto
      for (uint16_t j = 0; j < byte_to_reads; j++)
        image_data[byte_pointer++] = buffer[j];
      free(buffer);
      buffer = NULL;
    }

    for (uint32_t i = 0; i < HEADER_INFO_LENGTH + ext_length; i++) {
      printf("[%4d]: %3u -> %s -> %02X\n", i, image_data[i],
             uint8_t_to_binary_string(image_data[i]), image_data[i]);
    }

    char *output_filename = (char *)malloc(sizeof(char));
    sprintf(output_filename, "%s_%llu.png", base_output_filename, chunk);
    write_png_file(output_filename);

    memset(image_data, 0, width * height * BYTES_PER_PIXEL);

    // Libero la memoria dell'immagine per la prossima iterazione
    free(output_filename);
    output_filename = NULL;
  }

  free(image_data);
  image_data = NULL;
  fclose(fp);
}

// Non serve a molto questa funzione, è solo per debug
void recover_filename(FILE *fp) {
  // Ottieni il file descriptor
  int fd = fileno(fp);

  // Buffer per memorizzare il percorso del file
  char file_path[PATH_MAX];

  // Ottieni il percorso del file associato al file descriptor
  if (fcntl(fd, F_GETPATH, file_path) != -1) {
    printf("Percorso assoluto del file: %s\n", file_path);
  } else {
    perror("Error getting file path");
  }
}

char *generate_random_string(const uint8_t length) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd == -1) {
    perror("Failed to open /dev/urandom");
    exit(EXIT_FAILURE);
  }

  // Alloco la stringa e aggiungo uno spazio per mettere il carattere
  // terminatore
  char *str = (char *)malloc(sizeof(char) * (length + 1));

  // Legge byte casuali da /dev/urandom
  if (read(fd, str, length) != length) {
    perror("Failed to read from /dev/urandom");
    close(fd);
    free(str);
    exit(EXIT_FAILURE);
  }

  close(fd);

  // Converte i byte in caratteri ASCII stampabili
  for (uint8_t i = 0; i < length; i++) {
    // A caso scelgo se il carattere deve essere maiuscolo oppure minuscolo
    uint8_t upper_char = rand() % 2;
    if (upper_char == 0)
      str[i] = 'A' + ((uint8_t)str[i] % 26); // Genera caratteri da 'A' a 'Z'
    else
      str[i] = 'a' + ((uint8_t)str[i] % 26); // Genera caratteri da 'A' a 'Z'
  }

  str[length + 1] = '\0'; // Aggiunge il terminatore di stringa
  return str;
}

// Crea una cartella temporanea
char *create_temp_dir() {
  // Template per il nome della directory temporanea
  char template[] = "/tmp/tmpdir.XXXXXX";

  // Alloco memoria per il nome della directory temporanea
  char *tmp_dirname = (char *)malloc(strlen(template) + 1);
  if (!tmp_dirname) {
    perror("malloc error: ");
    exit(EXIT_FAILURE);
  }

  // Copio il template nella memoria allocata
  strcpy(tmp_dirname, template);

  // Creazione della directory temporanea
  if (mkdtemp(tmp_dirname) == NULL) {
    perror("tempdir: error: Could not create tmp directory");
    // Libero la memoria allocata in caso di errore
    free(tmp_dirname);
    exit(EXIT_FAILURE);
  }

  // Cambio della directory
  if (chdir(tmp_dirname) == -1) {
    perror("tempdir: error ->");
    // Libero la memoria allocata in caso di errore
    free(tmp_dirname);
    exit(EXIT_FAILURE);
  }

  // Restituisco il puntatore al percorso temporaneo
  return tmp_dirname;
}

// Elimina la cartella temporanea e tutti i file al suo interno
int delete_temp_dir(char *tmp_dirname) {
  if (nftw(tmp_dirname, remove_callback, FOPEN_MAX,
           FTW_DEPTH | FTW_MOUNT | FTW_PHYS) == -1) {
    perror("tempdir: error ->");
    // Libero la memoria allocata dopo aver rimosso la directory
    free(tmp_dirname);
    exit(EXIT_FAILURE);
  }

  // Libero la memoria allocata dopo aver cancellato la directory
  free(tmp_dirname);
  return 0;
}

int main(int argc, char *argv[]) {
  // Apre il file per la scrittura in modalità lettura binaria
  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    printf("File not found\n");
    exit(EXIT_FAILURE);
  }

  // Inizializza il generatore di numeri casuali
  srand(time(NULL));

  // printf("Size of png_byte: %lu\n", sizeof(png_byte));
  // printf("Size of png_bytep: %lu\n", sizeof(png_bytep));
  // printf("Extension length: %d\n", get_extension_length(argv[1]));
  // printf("Extension name: %s\n", get_extension_string(argv[1]));
  // printf("Stringa randomica: %s\n", generate_random_string(10));

  convert_file(fp, argv[1], argv[2]);

  /*
  FILE *temp_fp = tmpfile();
  FILE *temp_fp2 = tmpfile();
  if (temp_fp == NULL) {
    perror("tmpfile failed");
    return 1;
  }
  if (temp_fp2 == NULL) {
    perror("tmpfile failed");
    return 1;
  }
  recover_filename(temp_fp);
  recover_filename(temp_fp2);
  fclose(temp_fp);
  fclose(temp_fp2);
  */

  // Su macos la creazione, il cambio di working directory e la cancellazione
  // del file funzionano correttamente
  /*char *temp_dir = create_temp_dir();
  printf("Cartella creata: %s\n", temp_dir);
  delete_temp_dir(temp_dir);*/

  return EXIT_SUCCESS;
}
