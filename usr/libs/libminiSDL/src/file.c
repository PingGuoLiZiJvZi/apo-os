#include <SDL.h>

#include <stdlib.h>
#include <string.h>

static int64_t rw_file_size(SDL_RWops *f) {
  long cur = ftell(f->fp);
  fseek(f->fp, 0, SEEK_END);
  long end = ftell(f->fp);
  fseek(f->fp, cur, SEEK_SET);
  return (int64_t)end;
}

static int64_t rw_file_seek(SDL_RWops *f, int64_t offset, int whence) {
  if (fseek(f->fp, (long)offset, whence) != 0) return -1;
  return (int64_t)ftell(f->fp);
}

static size_t rw_file_read(SDL_RWops *f, void *buf, size_t size, size_t nmemb) {
  return fread(buf, size, nmemb, f->fp);
}

static size_t rw_file_write(SDL_RWops *f, const void *buf, size_t size, size_t nmemb) {
  return fwrite(buf, size, nmemb, f->fp);
}

static int rw_file_close(SDL_RWops *f) {
  int ret = 0;
  if (f->fp) ret = fclose(f->fp);
  free(f);
  return ret;
}

static int64_t rw_mem_size(SDL_RWops *f) {
  return f->mem.size;
}

static int64_t rw_mem_seek(SDL_RWops *f, int64_t offset, int whence) {
  int64_t base = 0;
  if (whence == RW_SEEK_CUR) base = f->mem.off;
  else if (whence == RW_SEEK_END) base = f->mem.size;
  int64_t target = base + offset;
  if (target < 0) target = 0;
  if (target > f->mem.size) target = f->mem.size;
  f->mem.off = target;
  return target;
}

static size_t rw_mem_read(SDL_RWops *f, void *buf, size_t size, size_t nmemb) {
  size_t bytes = size * nmemb;
  int64_t left = f->mem.size - f->mem.off;
  if (left <= 0) return 0;
  if ((int64_t)bytes > left) bytes = (size_t)left;
  memcpy(buf, (uint8_t *)f->mem.base + f->mem.off, bytes);
  f->mem.off += (int64_t)bytes;
  return size == 0 ? 0 : (bytes / size);
}

static size_t rw_mem_write(SDL_RWops *f, const void *buf, size_t size, size_t nmemb) {
  size_t bytes = size * nmemb;
  int64_t left = f->mem.size - f->mem.off;
  if (left <= 0) return 0;
  if ((int64_t)bytes > left) bytes = (size_t)left;
  memcpy((uint8_t *)f->mem.base + f->mem.off, buf, bytes);
  f->mem.off += (int64_t)bytes;
  return size == 0 ? 0 : (bytes / size);
}

static int rw_mem_close(SDL_RWops *f) {
  free(f);
  return 0;
}

SDL_RWops *SDL_RWFromFile(const char *filename, const char *mode) {
  FILE *fp = fopen(filename, mode);
  if (!fp) return NULL;
  SDL_RWops *rw = (SDL_RWops *)malloc(sizeof(SDL_RWops));
  if (!rw) {
    fclose(fp);
    return NULL;
  }
  memset(rw, 0, sizeof(*rw));
  rw->type = RW_TYPE_FILE;
  rw->fp = fp;
  rw->size = rw_file_size;
  rw->seek = rw_file_seek;
  rw->read = rw_file_read;
  rw->write = rw_file_write;
  rw->close = rw_file_close;
  return rw;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size) {
  if (!mem || size < 0) return NULL;
  SDL_RWops *rw = (SDL_RWops *)malloc(sizeof(SDL_RWops));
  if (!rw) return NULL;
  memset(rw, 0, sizeof(*rw));
  rw->type = RW_TYPE_MEM;
  rw->mem.base = mem;
  rw->mem.size = size;
  rw->mem.off = 0;
  rw->size = rw_mem_size;
  rw->seek = rw_mem_seek;
  rw->read = rw_mem_read;
  rw->write = rw_mem_write;
  rw->close = rw_mem_close;
  return rw;
}
