#include "xmpblock.h"
#include <stdio.h>  // fopen, fclose, fread, fseek, ftell, getc, NULL
#include <stdlib.h> // malloc, realloc, free, getdelim, size_t
#include <string.h> // memcmp, strcmp
#include <unistd.h> // unlink, if failure writing
#include <fcntl.h>  // open, for exclusive creation
#include <ctype.h>  // isspace

// runtime-changeable configuration; must be >= 1; 2000 recommended
int xmp_writable_padding = 2000;

////////////////////////////// HELPERS //////////////////////////////
static void add_packet(xmp_rdata *to, char *packet) {
    to->num_packets += 1;
    to->packets = realloc(to->packets, to->num_packets * sizeof(char*));
    to->packets[to->num_packets - 1] = packet;
}

static long ru8(FILE *f, int littleendian) { return getc(f); }
static long ru16(FILE *f, int littleendian) {
    if (littleendian) return getc(f) | (getc(f)<<8);
    else return (getc(f)<<8) | getc(f);
}
static long ru24(FILE *f, int littleendian) {
    if (littleendian) return getc(f) | (getc(f)<<8) | (getc(f)<<16);
    else return (getc(f)<<16) | (getc(f)<<8) | getc(f);
}
static long ru32(FILE *f, int littleendian) {
    if (littleendian) return getc(f) | (getc(f)<<8) | (getc(f)<<16) | (getc(f)<<24);
    else return (getc(f)<<24) | (getc(f)<<16) | (getc(f)<<8) | getc(f);
}
static size_t ru64(FILE *f, int le) {
    if (le) return ((size_t)ru32(f,le)) | (((size_t)ru32(f,le))<<32);
    else return (((size_t)ru32(f,le))<<32) | ((size_t)ru32(f,le));
}

static void wu8(unsigned char val, FILE *f, int littleendian) { putc(val, f); }
static void wu16(unsigned short val, FILE *f, int littleendian) {
    if (littleendian) {
        putc(0xFF & val, f);
        putc(0xFF & (val>>8), f);
    } else {
        putc(0xFF & (val>>8), f);
        putc(0xFF & val, f);
    }
}
static void wu24(unsigned int val, FILE *f, int littleendian) {
    if (littleendian) {
        putc(0xFF & val, f);
        putc(0xFF & (val>>8), f);
        putc(0xFF & (val>>16), f);
    } else {
        putc(0xFF & (val>>16), f);
        putc(0xFF & (val>>8), f);
        putc(0xFF & val, f);
    }
}
static void wu32(unsigned int val, FILE *f, int littleendian) {
    if (littleendian) {
        putc(0xFF & val, f);
        putc(0xFF & (val>>8), f);
        putc(0xFF & (val>>16), f);
        putc(0xFF & (val>>24), f);
    } else {
        putc(0xFF & (val>>24), f);
        putc(0xFF & (val>>16), f);
        putc(0xFF & (val>>8), f);
        putc(0xFF & val, f);
    }
}
static void wu64(size_t val, FILE *f, int le) {
    if (le) {
        wu32(val&0xFFFFFFFF, f, le);
        wu32((val>>32)&0xFFFFFFFF, f, le);
    } else {
        wu32((val>>32)&0xFFFFFFFF, f, le);
        wu32(val&0xFFFFFFFF, f, le);
    }
}

static _Thread_local char copy_buffer[4096];
static int copy_bytes(FILE *from, FILE *to, size_t bytes) {
    while(bytes > 0) {
        size_t got = fread(copy_buffer, 1, sizeof(copy_buffer) > bytes ? bytes : sizeof(copy_buffer), from);
        if (got == 0) return 0;
        if (got != fwrite(copy_buffer, 1, got, to)) return 0;
        bytes -= got;
    }
    return 1;
}
////////////////////////////// HELPERS //////////////////////////////

////////////////////////////// WRAPPING /////////////////////////////
static size_t place_block(FILE *t, const char *data, int wrap, int pad) {
    long old = ftell(t);
    if (wrap)
        fputs("<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n", t);
    fputs(data, t);
    for(int i=1; i<pad; i+=1)
        putc((i%100) ? ' ' : '\n', t);
    if (wrap) {
        if (pad) fputs("\n<?xpacket end=\"w\"?>", t);
        else fputs("\n<?xpacket end=\"r\"?>", t);
    }
    return ftell(t) - old;
}
static size_t placed_size_of_block(const char *data, int wrap, int pad) {
    size_t wrote = 0;
    if (wrap) wrote += 54;
    wrote += strlen(data);
    wrote += (pad > 0) ? pad - 1 : 0;
    if (wrap) wrote += 20;
    return wrote;
}
static char *read_block(FILE *f, long fpos, long size) {
    char buf[256];
    long start = fpos, end = fpos + size;

    // skip leading whitespace
    fseek(f, start, SEEK_SET);
    while (isspace(getc(f)) && start < end) start += 1;
    
    // if present, skip xpacket header (even if malformed)
    fseek(f, start, SEEK_SET);
    fread(buf, 1, 16, f);
    if (!memcmp(buf, "<?xpacket begin=", 16)) {
        while (getc(f) != '?') {}
        if (getc(f) != '>') return 0;
        start = ftell(f);
        // and whitespace after it
        while (isspace(getc(f)) && start < end) start += 1;
    }
    
    // skip trailing whitespace
    int i = -1;
    while(i < 0 && end > start) {
        fseek(f, end-sizeof(buf), SEEK_SET);
        fread(buf, 1, sizeof(buf), f);
        for(i=sizeof(buf)-1; i>=0 && isspace(buf[i]); i-=1)
            end -= 1;
    }
    // if present, skip xpacket footer (even if malformed)
    fseek(f, end-19, SEEK_SET);
    fread(buf, 1, 19, f); 
    if (!memcmp(buf, "<?xpacket end=", 14) && !memcmp(buf+17, "?>", 2)) {
        end -= 19;
        // skip more trailing whitespace
        i = -1;
        while(i < 0 && end > start) {
            fseek(f, end-sizeof(buf), SEEK_SET);
            fread(buf, 1, sizeof(buf), f);
            for(i=sizeof(buf)-1; i>=0 && isspace(buf[i]); i-=1)
                end -= 1;
        }
    }
    if (end > start) {
        char *ans = malloc(end-start+1);
        fseek(f, start, SEEK_SET);
        fread(ans, 1, end-start, f);
        ans[end-start] = '\0';
        fseek(f, fpos + size, SEEK_SET);
        return ans;
    } else {
        fseek(f, fpos + size, SEEK_SET);
        return 0;
    }
}
static char *read_block_delim(FILE *f, long fpos, char delim, size_t *end) {
    fseek(f, fpos, SEEK_SET);
    while (getc(f) != delim && !feof(f)) {}
    if (end) *end = ftell(f)-1;
    return read_block(f, fpos, ftell(f)-fpos-1);
}
////////////////////////////// WRAPPING /////////////////////////////


//////////////////////////////// GIF ////////////////////////////////
xmp_rdata xmp_from_gif(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    int endian = 1;

    char header[6];
    fread(header, 1, 6, f);
    int mode = 0;
    if (memcmp(header, "GIF89a", 6) == 0) mode = 2;
    if (memcmp(header, "GIF87a", 6) == 0) mode = 1;
    if (!mode) goto malformed;

    ans.width = ru16(f, endian);
    ans.height = ru16(f, endian);
    if (mode < 2) goto end;
    unsigned char flags = ru8(f, endian);
    fseek(f, 2, SEEK_CUR);
    if (flags & 0x80) fseek(f, 6<<(flags&0x7), SEEK_CUR);

    for(;;) {
        unsigned char intro = ru8(f, endian);
        if (intro == 0x3B) goto end;
        else if (intro == 0x2C) {
            fseek(f, 8, SEEK_CUR);
            flags = ru8(f, endian);
            if (flags & 0x80) fseek(f, 6<<(flags&0x7), SEEK_CUR);
            fseek(f, 1, SEEK_CUR);
            long len = ru8(f, endian);
            if (len < 0 || len > 255) goto malformed;
            while (len) {
                fseek(f, len, SEEK_CUR);
                len = ru8(f, endian);
                if (len < 0 || len > 255) goto malformed;
            }
        } else if (intro == 0x21) {
            long label = ru8(f, endian);
            if (label == 0xFF) {
                if (ru8(f, endian) != 11) goto malformed;
                char appid[11]; fread(appid, 1, 11, f);
                if (!memcmp(appid, "XMP DataXMP", 11)) {
                    size_t len = 0;
                    char *packet = read_block_delim(f, ftell(f), 1, &len);
                    add_packet(&ans, packet);
                    getc(f); // delimiter, already processed
                    unsigned char trailer[257]; fread(trailer, 1, 257, f);
                    for(int i=0; i<256; i+=1) if (trailer[i] != 0xFF - i) goto malformed;
                    if (trailer[256]) goto malformed;
                } else {
                    long len = ru8(f, endian);
                    if (len < 0 || len > 255) goto malformed;
                    while (len) {
                        fseek(f, len, SEEK_CUR);
                        len = ru8(f, endian);
                        if (len < 0 || len > 255) goto malformed;
                    }
                }
            } else {
                long len = ru8(f, endian);
                if (len < 0 || len > 255) goto malformed;
                while (len) {
                    fseek(f, len, SEEK_CUR);
                    len = ru8(f, endian);
                    if (len < 0 || len > 255) goto malformed;
                }
            }
        } else {
            goto malformed;
        }
    }

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}

int xmp_to_gif(const char *ref, const char *dest, const char *xmp) {
    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    char bigbuf[258];
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");
    int endian = 1;
    int wrote_xmp = (xmp == NULL);

    fseek(f, 6, SEEK_CUR);
    fwrite("GIF89a", 1, 6, t);

    copy_bytes(f,t,4);
    unsigned char flags = ru8(f, endian);
    wu8(flags, t, endian);
    if (flags & 0x80) copy_bytes(f, t, 2 + (6<<(flags&0x7)));
    else copy_bytes(f, t, 2);

    for(;;) {
        unsigned char intro = ru8(f, endian);
        if (intro == 0x3B) {
            if (!wrote_xmp) {
                wu8(0x21, t, endian);
                wu8(0xFF, t, endian);
                fwrite("XMP DataXMP", 1, 11, t);
                place_block(t, xmp, 1, xmp_writable_padding);
                bigbuf[0] = 1; bigbuf[256] = bigbuf[257] = 0;
                for(int i=0; i<256; i+=1) bigbuf[i] = 0xFF - i;
                fwrite(bigbuf, 1, 258, t);
            }
            wu8(intro, t, endian);
            goto end;
        }
        else if (intro == 0x2C) {
            wu8(intro, t, endian);
            fread(bigbuf, 1, 8, f);
            fwrite(bigbuf, 1, 8, t);
            unsigned char flag = ru8(f, endian);
            wu8(flag, t, endian);
            if (flag&0x80) 
                if (!copy_bytes(f, t, (6<<(flags&0x7)))) goto malformed;
            copy_bytes(f, t, 1);
            unsigned char length = ru8(f, endian);
            wu8(length, t, endian);
            while(length) {
                if (!copy_bytes(f, t, length)) goto malformed;
                length = ru8(f, endian);
                wu8(length, t, endian);
            }
        }
        else if (intro == 0x21) {
            unsigned char label = ru8(f, endian);
            if (label == 0xFF) {
                unsigned char tmp = ru8(f, endian);
                if (tmp != 11) goto malformed;
                fread(bigbuf, 1, 11, f);
                if (!memcmp(bigbuf, "XMP DataXMP", 11)) {
                    unsigned char length = ru8(f, endian);
                    while(length) {
                        fseek(f, length, SEEK_CUR);
                        length = ru8(f, endian);
                    }
                    if (!wrote_xmp) {
                        wu8(0x21, t, endian);
                        wu8(0xFF, t, endian);
                        wu8(tmp, t, endian);
                        fwrite("XMP DataXMP", 1, 11, t);
                        place_block(t, xmp, 1, xmp_writable_padding);
                        bigbuf[0] = 1; bigbuf[256] = bigbuf[257] = 0;
                        for(int i=0; i<256; i+=1) bigbuf[i+1] = 0xFF - i;
                        fwrite(bigbuf, 1, 258, t);
                        wrote_xmp = 1;
                    }
                } else {
                    wu8(0x21, t, endian);
                    wu8(0xFF, t, endian);
                    unsigned char length = ru8(f, endian);
                    wu8(length, t, endian);
                    while(length) {
                        if (!copy_bytes(f, t, length)) goto malformed;
                        length = ru8(f, endian);
                        wu8(length, t, endian);
                    }
                }
            } else {
                wu8(intro, t, endian);
                wu8(label, t, endian);
                unsigned char length = ru8(f, endian);
                wu8(length, t, endian);
                while(length) {
                    if (!copy_bytes(f, t, length)) goto malformed;
                    length = ru8(f, endian);
                    wu8(length, t, endian);
                }
            }
        } else {
            goto malformed;
        }
    }


malformed:
    fclose(f);
    fclose(t);
    close(fd);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    close(fd);
    return 1;
}
//////////////////////////////// GIF ////////////////////////////////


/////////////////////////////// ISOBMF //////////////////////////////
typedef struct { long length; char type[4]; long fpos; } isobmf_box;

static isobmf_box isobmf_read_box(FILE *f, long end) {
    isobmf_box box;
    box.length = ru32(f, 0);
    fread(box.type, 1, 4, f);
    if (box.length == 1)
        box.length = ru64(f, 0) - 8;
    box.fpos = ftell(f);
    if (box.length == 0) box.length = end - box.fpos;
    else if (box.length > 0) box.length -= 8;
    return box;
}

xmp_rdata xmp_from_isobmf(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    int endian = 0;
    int format = 0; // 0 = unknown, 1 = JPEG2000, 2 = HEIC, 3 = AVIF
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    isobmf_box box = isobmf_read_box(f, fsize);
    if (!memcmp(box.type, "jP  ", 4) && box.length == 4) {
        char bit[4];
        fread(bit, 1, 4, f);
        if (!memcmp(bit, "\r\n\x87\n", 4)) format = 1;
        else goto malformed;
    } else if (!memcmp(box.type, "ftyp", 4) && box.length >= 12) {
        fseek(f, box.fpos + 8, SEEK_SET);
        char bit[4];
        for(int i=0; i<(box.length-8)>>2; i+=1) {
            fread(bit, 1, 4, f);
            if (!memcmp(bit, "heic", 4)) {
                format = 2;
            } else if (!memcmp(bit, "avif", 4)) {
                format = 3;
            }
        }
    } else goto malformed; // add other cases if other isobmf supported

    fseek(f, box.length + box.fpos, SEEK_SET);
    
    for(;;) {
        box = isobmf_read_box(f, fsize);
        if (box.length < 0) goto end;
        if (box.length + box.fpos > fsize) goto malformed;
        if (format == 1 && !memcmp(box.type, "jp2h", 4)) {
            while(ftell(f) < box.fpos+box.length) {
                isobmf_box inner = isobmf_read_box(f, box.length + box.fpos);
                if (inner.length < 0) goto malformed;
                if (inner.length + inner.fpos > box.length + box.fpos) goto malformed;
                if (!memcmp(inner.type, "ihdr", 4)) {
                    ans.height = ru32(f, endian);
                    ans.width = ru32(f, endian);
                }
                fseek(f, inner.fpos+inner.length, SEEK_SET);
            }
            fseek(f, box.fpos+box.length, SEEK_SET);
        } else if ((format == 2 || format == 3) && !memcmp(box.type, "meta", 4)) {
            fseek(f, 4, SEEK_CUR); // skip 4 bytes, not sure why
            while(ftell(f) < box.fpos+box.length) {
                isobmf_box inner = isobmf_read_box(f, box.length + box.fpos);
                if (inner.length < 0) goto malformed;
                if (inner.length + inner.fpos > box.length + box.fpos) goto malformed;
                if (!memcmp(inner.type, "idat", 4)) {
                    fseek(f, inner.fpos+4, SEEK_SET);
                    ans.width = ru16(f, endian);
                    ans.height = ru16(f, endian);
                }
                else if (!memcmp(inner.type, "iprp", 4)) {
                    while(ftell(f) < inner.fpos+inner.length) {
                        isobmf_box in2 = isobmf_read_box(f, inner.length + inner.fpos);
                        if (!memcmp(in2.type, "ipco", 4)) {
                            while(ftell(f) < in2.fpos+in2.length) {
                                isobmf_box in3 = isobmf_read_box(f, in2.length + in2.fpos);
                                if (!memcmp(in3.type, "ispe", 4)) {
                                    fseek(f, in3.fpos+4, SEEK_SET);
                                    ans.width = ru32(f, endian);
                                    ans.height = ru32(f, endian);
                                }
                                fseek(f, in3.fpos+in3.length, SEEK_SET);
                                
                            }
                        }
                        fseek(f, in2.fpos+in2.length, SEEK_SET);
                    }
                }
                fseek(f, inner.fpos+inner.length, SEEK_SET);
            }
            fseek(f, box.fpos+box.length, SEEK_SET);
        } else if (!memcmp(box.type, "uuid", 4)) {
            unsigned char uuid[16];
            fread(uuid, 1, 16, f);
            unsigned char ref[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};
            if (!memcmp(uuid, ref, 16)) {
                char *xmp = read_block(f, ftell(f), box.length-16);
                if (xmp) add_packet(&ans, xmp);
            }
        }
        fseek(f, box.length + box.fpos, SEEK_SET);
    }
    goto end;

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}

static void isobmf_write_xmp(FILE *t, const char *xmp) {
    static const unsigned char refuuid[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

    wu32(24 + placed_size_of_block(xmp,1,xmp_writable_padding), t, 0);
    fwrite("uuid", 1, 4, t);
    fwrite(refuuid, 1, 16, t);
    place_block(t, xmp, 1, xmp_writable_padding);
}

int xmp_to_isobmf(const char *ref, const char *dest, const char *xmp) {
    static const unsigned char refuuid[16] = {0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC};

    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");
    int endian = 0;
    int wrote_xmp = (xmp == NULL);

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    while (!feof(f)) {
        unsigned length1 = ru32(f, endian);
        if (length1 == -1) break;
        if (length1 == 0 && !wrote_xmp) {
            // length 0 mean "to end of file". I can convert to a numeric length, but every jp2 file I've checked used 0 for the jp2c box, so I've decided not to change that. If this is the last box, write XMP before it (and if it was XMP it will be skipped below)
            isobmf_write_xmp(t, xmp);
            wrote_xmp = 1;
        }
        char type[4]; fread(type, 1, 4, f);

        long length2 = length1 + 8;
        if (length1 == 1) length2 = ru64(f, endian);
        if (length1 == 0) length2 = 16 + (fsize - ftell(f));
        
        if (!memcmp(type, "uuid", 4)) {
            unsigned char uuid[16];
            fread(uuid, 1, 16, f);
            if (!memcmp(uuid, refuuid, 16)) {
                if (!wrote_xmp) {
                    isobmf_write_xmp(t,xmp);
                    wrote_xmp = 1;
                }
                fseek(f, length2-32, SEEK_CUR);
            } else {
                wu32(length1, t, endian);
                fwrite(type, 1, 4, t);
                if (length1 == 1) wu64(length2, t, endian);
                fwrite(uuid, 1, 16, t);
                if (!copy_bytes(f, t, length2-32)) goto malformed;
            }
        } else {
            wu32(length1, t, endian);
            fwrite(type, 1, 4, t);
            if (length1 == 1) wu64(length2, t, endian);
            if (!copy_bytes(f, t, length2-16)) goto malformed;
        }
    }
    if (!wrote_xmp) isobmf_write_xmp(t,xmp);

    goto end;

malformed:
    fclose(f);
    fclose(t);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    return 1;
}
/////////////////////////////// ISOBMF //////////////////////////////

//////////////////////////////// JPEG ///////////////////////////////
xmp_rdata xmp_from_jpeg(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    int endian = 0;
    char *extended = NULL;

    if (ru8(f, endian) != 0xFF) goto malformed;
    if (ru8(f, endian) != 0xD8) goto malformed;
    

    long m0 = ru8(f, endian);
    while(!feof(f) && m0 >= 0) {
        long m1 = ru8(f, endian);
        if (m0 == 0xFF && m1 == 0xE1) {
            long len = ru16(f, endian);
            char buf[35];
            size_t got = fread(buf, 1, 35, f);
            if (got > 28 && !strncmp(buf, "http://ns.adobe.com/xap/1.0/", 29)) {
                char *packet = read_block(f, ftell(f) + 29-got, len-31);
                if (packet) add_packet(&ans, packet);
            } else if (got > 34 && !strncmp(buf, "http://ns.adobe.com/xmp/extension/", 35)) {
                // XMP spec says JPEG has two packets, standard and extended; that the extended's GUID is marked; and that the extended follows the standard. But it fails to state that it has *only* two packets, or that all parts of the extended packet must be provided, or that the extended can't be moved earlier.
                // To avoid needing a GUID:packet mapping, I assume:
                // 1. standard, then GUID
                // 2. only one GUID matches standard
                // 3. all that GUID's parts are present
                // As I have yet to find an extended XMP in the wild, I haven't been able to test these assumptions
                if (!ans.packets) {
                    fprintf(stderr, "WARNING: extended XMP found with no standard XMP; extended ignored\n");
                    fseek(f, len - 2 - got, SEEK_CUR);
                } else {
                    char guid[33];
                    fread(guid, 1, 32, f);
                    guid[32] = '\0';
                    if (strstr(ans.packets[0], guid)) {
                        long ext_len = ru32(f, endian);
                        if (!extended) extended = calloc(ext_len, 1);
                        long ext_off = ru32(f, endian);
                        fread(extended + ext_off, 1, len-77, f);
                    } else {
                        fprintf(stderr, "WARNING: extended XMP found with GUID not matching XMP; ignored\n");
                        fseek(f, len - 2 - got, SEEK_CUR);
                    }
                }
            } else {
                fseek(f, len - 2 - got, SEEK_CUR);
            }
        } else if (m0 == 0xFF && 0xC0 <= m1 && m1 <= 0xCF
            && m1 != 0xC4
            && m1 != 0xCC
        ) {
            fseek(f, 3, SEEK_CUR);
            // can contain thumbnails, so look for max
            unsigned tmp = ru16(f, endian);
            if (tmp > ans.height) ans.height = tmp;
            tmp = ru16(f, endian);
            if (tmp > ans.width) ans.width = tmp;
        } else if (m0 == 0xFF && m1 == 0xDC) {
            fseek(f, 2, SEEK_CUR);
            unsigned tmp = ru16(f, endian);
            if (tmp > ans.height) ans.height = tmp;
        }
        m0 = m1;
    }
    if (extended) add_packet(&ans, extended);
    goto end;
    

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    if (extended) free(extended);
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}

static void jpeg_write_xmp(FILE *t, const char *xmp, const char *ext) {
    wu8(0xFF, t, 0);
    wu8(0xE1, t, 0);
    wu16(placed_size_of_block(xmp, 1, xmp_writable_padding)+31, t, 0);
    fwrite("http://ns.adobe.com/xap/1.0/", 1, 29, t);
    place_block(t, xmp, 1, xmp_writable_padding);
    if (ext) {
        size_t total = strlen(ext);
        size_t parts = total/65400 + 1;
        for(int i=0; i<parts; i+=1) {
            size_t start = total*i/parts;
            size_t end = total*(i+1)/parts;
            wu8(0xFF, t, 0);
            wu8(0xE1, t, 0);
            wu16((end-start)+37, t, 0);
            fwrite("http://ns.adobe.com/xmp/extension/", 1, 35, t);
            fwrite(ext-start, 1, (end-start), t);
        }
    }
}
int xmp_to_jpeg_ext(const char *ref, const char *dest, const char *xmp, const char *ext) {

    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");
    int endian = 0;
    int wrote_xmp = (xmp == NULL);

    if (ru8(f,endian) == 0xFF) wu8(0xFF, t, endian); else goto malformed;
    if (ru8(f,endian) == 0xD8) wu8(0xD8, t, endian); else goto malformed;
    
    long m0 = ru8(f, endian);
    while (!feof(f) && m0 >= 0) {
        long m1 = ru8(f, endian);
        if (m0 == 0xFF && m1 == 0xE1) {
            long len = ru16(f, endian);
            char buf[35];
            size_t got = fread(buf, 1, 35, f);
            if (got > 28 && !strncmp(buf, "http://ns.adobe.com/xap/1.0/", 29)) {
                fseek(f, len - 2 - got, SEEK_CUR);
                if (!wrote_xmp) jpeg_write_xmp(t, xmp, ext);
                wrote_xmp = 1;
            } else if (got > 34 && !strncmp(buf, "http://ns.adobe.com/xmp/extension/", 35)) {
                fseek(f, len - 2 - got, SEEK_CUR);
            } else {
                fseek(f, -4 - got, SEEK_CUR);
                copy_bytes(f, t, len+2);
            }
            m1 = ru8(f, endian);
        }
        else if (m0 == 0xFF && m1 == 0xED && !wrote_xmp) {
            long len = ru16(f, endian);
            char buf[14];
            size_t got = fread(buf, 1, 14, f);
            if (got > 13 && !strncmp(buf, "Photoshop 3.0", 14)) {
                jpeg_write_xmp(t, xmp, ext);
                wrote_xmp = 1;
            }
            fseek(f, -4 - got, SEEK_CUR);
            copy_bytes(f, t, len+2);
            m1 = ru8(f, endian);
        }
        else if (m0 == 0xFF && 0xC0 <= m1 && m1 <= 0xCF
            && m1 != 0xC4
            && m1 != 0xCC
            && !wrote_xmp
        ) {
            jpeg_write_xmp(t, xmp, ext);
            wrote_xmp = 1;
            wu8(m0, t, endian);
        }
        else { 
            wu8(m0, t, endian);
        }
        m0 = m1;
    }
    goto end;
    
malformed:
    fclose(f);
    fclose(t);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    return 1;
}
int xmp_to_jpeg(const char *ref, const char *dest, const char *xmp) {
    return xmp_to_jpeg_ext(ref, dest, xmp, NULL);
}
//////////////////////////////// JPEG ///////////////////////////////
    

//////////////////////////////// PNG ////////////////////////////////
static unsigned crc_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};
static unsigned feed_crc_buf(unsigned c, unsigned char *buf, int len) {
    for(int i=0; i<len; i+=1)
        c = crc_table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c;
}
static unsigned feed_crc_str(unsigned c, const char *s) {
    while(*s)
        c = crc_table[(c ^ (unsigned)(*(s++))) & 0xff] ^ (c >> 8);
    return c;
}
static unsigned init_crc() { return 0xffffffffu; }
static unsigned feed_crc_u32(unsigned c, unsigned short x) {
    c = crc_table[(c^((x>>24)&0xFF)) & 0xff] ^ (c>>8);
    c = crc_table[(c^((x>>16)&0xFF)) & 0xff] ^ (c>>8);
    c = crc_table[(c^((x>>8)&0xFF)) & 0xff] ^ (c>>8);
    c = crc_table[(c^(x&0xFF)) & 0xff] ^ (c>>8);
    return c;
}
static unsigned finish_crc(unsigned c) { return c ^ 0xffffffffu; }

xmp_rdata xmp_from_png(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    int endian = 0;
    unsigned crc;
    unsigned char buf[22];

    fread(buf, 1, 8, f);
    if (memcmp(buf, "\x89PNG\r\n\x1a\n", 8)) goto malformed;
    
    if (ru32(f, endian) != 13) goto malformed;
    crc = init_crc();
    fread(buf, 1, 4, f); crc=feed_crc_buf(crc, buf, 4);
    if (memcmp(buf, "IHDR", 4)) goto malformed;
    ans.width = ru32(f, endian); crc=feed_crc_u32(crc, ans.width);
    ans.height = ru32(f, endian); crc=feed_crc_u32(crc, ans.height);
    fread(buf, 1, 5, f); crc=feed_crc_buf(crc, buf, 5);
    if (ru32(f, endian) != finish_crc(crc)) goto malformed;
    
    while(!feof(f)) {
        unsigned length = ru32(f, endian);
        if (feof(f) || length < 0) break;
        if (length > 0x7fffffff) goto malformed;
        fread(buf, 1, 4, f);
        if (!memcmp(buf, "iTXt", 4) && length > 22) {
            fread(buf, 1, 22, f);
            if (!memcmp(buf, "XML:com.adobe.xmp\0\0\0\0\0", 22)) {
                char *xmp = read_block(f, ftell(f), length-22);
                if (xmp) add_packet(&ans, xmp);
            } else {
                fseek(f, length-22, SEEK_CUR);
            }
        } else {
            fseek(f, length, SEEK_CUR);
        }
        ru32(f, endian);
    }
    
    goto end;
    

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}

int xmp_to_png(const char *ref, const char *dest, const char *xmp) {
    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");
    int endian = 0;

    char buf[22];

    if (!copy_bytes(f, t, 33)) goto malformed;

    if (xmp) {
        wu32((strlen(xmp)+54+20)+22, t, endian);
        unsigned crc = init_crc();
        fwrite("iTXtXML:com.adobe.xmp\0\0\0\0\0", 1, 26, t);
        crc = feed_crc_buf(crc,(unsigned char *)"iTXtXML:com.adobe.xmp\0\0\0\0\0",26);
        
        fputs("<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n", t);
        crc = feed_crc_str(crc, "<?xpacket begin=\"﻿\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
        fputs(xmp, t);
        crc = feed_crc_str(crc,xmp);
        fputs("\n<?xpacket end=\"r\"?>", t);
        crc = feed_crc_str(crc,"\n<?xpacket end=\"r\"?>");
        
        wu32(finish_crc(crc), t, endian);
    }
    
    while(!feof(f)) {
        unsigned length = ru32(f, endian);
        if (feof(f) || length < 0) break;
        if (length > 0x7fffffff) goto malformed;
        fread(buf, 1, 4, f);
        if (!memcmp(buf, "iTXt", 4) && length > 22) {
            fread(buf, 1, 22, f);
            if (!memcmp(buf, "XML:com.adobe.xmp\0\0\0\0\0", 22)) {
                fseek(f, length-18, SEEK_CUR);
            } else {
                fseek(f, -30, SEEK_CUR);
                if (!copy_bytes(f,t, 12+length)) goto malformed;
            }
        } else {
            fseek(f, -8, SEEK_CUR);
            if (!copy_bytes(f,t, 12+length)) goto malformed;
        }
    }
    goto end;
    
malformed:
    fclose(f);
    fclose(t);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    return 1;
}
//////////////////////////////// PNG ////////////////////////////////

//////////////////////////////// WEBP ///////////////////////////////
xmp_rdata xmp_from_webp(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    int endian = 1;
    char variant[4], fourcc[4];

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    fread(variant, 1, 4, f);
    if (memcmp(variant, "RIFF", 4)) goto malformed;
    if (ru32(f, endian) != fsize-8) goto malformed;
    fread(variant, 1, 4, f);
    if (memcmp(variant, "WEBP", 4)) goto malformed;
    fread(variant, 1, 4, f);
    unsigned length = ru32(f, endian);
    
    if (!memcmp(variant, "VP8 ", 4)) {
        fseek(f, 6, SEEK_CUR);
        ans.width = ru16(f, endian);
        ans.height = ru16(f, endian);
        goto end;
    } else if (!memcmp(variant, "VP8L", 4)) {
        if (ru8(f,endian) != 0x2F) goto malformed;
        unsigned packed = ru32(f, endian);
        ans.width = 1 + (packed & 0x3FFF);
        ans.height = 1 + ((packed>>14) & 0x3FFF);
        goto end;
    } else if (!memcmp(variant, "VP8X", 4)) {
        fseek(f, 4, SEEK_CUR);
        ans.width = 1 + ru24(f, endian);
        ans.height = 1 + ru24(f, endian);
        fseek(f, length - 10, SEEK_CUR);
        if (length & 1) fseek(f, 1, SEEK_CUR);
    } else goto malformed;
    
    while(!feof(f)) {
        if (fread(fourcc, 1, 4, f) != 4) break;
        unsigned length = ru32(f, endian);
        if (!memcmp(fourcc, "XMP ", 4)) {
            char *xmp = read_block(f, ftell(f), length);
            if (xmp) add_packet(&ans, xmp);
        } else {
            fseek(f, length, SEEK_CUR);
        }
        if (length&1) fseek(f, 1, SEEK_CUR);
    }
    goto end;

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}

int xmp_to_webp(const char *ref, const char *dest, const char *xmp) {
    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");
    int endian = 1;

    char fourcc[4], variant[4];

    if (!copy_bytes(f, t, 12)) goto malformed;
    fread(variant, 1, 4, f);
    unsigned length = ru32(f, endian);

    if (!memcmp(variant, "VP8 ", 4)) {
        fseek(f, 6, SEEK_CUR);
        unsigned width = ru16(f, endian);
        unsigned height = ru16(f, endian);

        fwrite("VP8X", 1, 4, t);
        wu32(10, t, endian);
        wu8(4, t, endian);
        wu24(0, t, endian);
        wu24(width-1, t, endian);
        wu24(height-1, t, endian);
        
        fseek(f, -18, SEEK_CUR);
        copy_bytes(f,t,length+8 + (length&1));
    } else if (!memcmp(variant, "VP8L", 4)) {
        if (ru8(f,endian) != 0x2F) goto malformed;
        unsigned packed = ru32(f, endian);
        unsigned width = 1 + (packed & 0x3FFF);
        unsigned  height = 1 + ((packed>>14) & 0x3FFF);
        int alpha = (packed >> 28)&1;
        
        fwrite("VP8X", 1, 4, t);
        wu32(10, t, endian);
        wu8(4 | (alpha<<4), t, endian);
        wu24(0, t, endian);
        wu24(width-1, t, endian);
        wu24(height-1, t, endian);
        
        fseek(f, -13, SEEK_CUR);
        copy_bytes(f,t,length+8 + (length&1));
    } else if (!memcmp(variant, "VP8X", 4)) {
        fwrite(variant, 1, 4, t);
        wu32(length, t, endian);
        wu8(4|ru8(f,endian), t, endian);
        copy_bytes(f, t, length-1 + (length&1));

        while(!feof(f)) {
            if (fread(fourcc, 1, 4, f) != 4) break;
            unsigned length = ru32(f, endian);
            if (!memcmp(fourcc, "XMP ", 4)) {
                fseek(f,length + (length&1),SEEK_CUR);
            } else {
                fseek(f, -8, SEEK_CUR);
                copy_bytes(f,t,length+8 + (length&1));
            }
        }
    } else goto malformed;
    
    fwrite("XMP ", 1, 4, t);
    length = placed_size_of_block(xmp, 1, xmp_writable_padding);
    wu32(length, t, endian);
    place_block(t, xmp, 1, xmp_writable_padding);
    if (length&1) putc(0, t);

    unsigned fsize = ftell(t);
    fseek(t, 4, SEEK_SET);
    wu32(fsize-8, t, endian);

    goto end;
    
malformed:
    fclose(f);
    fclose(t);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    return 1;
}
//////////////////////////////// WEBP ///////////////////////////////



//////////////////////////////// TIFF ///////////////////////////////
xmp_rdata xmp_from_tiff(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    
    static const char length_of_type[13] = {
        -1, // unused
        1, 1, 2, 4, 8, // unsigned byte/ascii/short/int/rational
        1, 1, 2, 4, 8, // signed byte/undef/short/int/rational
        4, 8 // float
    };
    /*
    static const char *name_of_type[13] = {
        "error",
        "u8", "ascii", "u16", "u32", "ru32/u32",
        "i8", "binary", "i16", "i32", "ri32/i32",
        "float", "double"
    };
    */
    
    char endflag[2]; fread(endflag, 1, 2, f);
    int endian = 1;
    if (!memcmp(endflag, "MM", 2)) endian = 0;
    else if (memcmp(endflag, "II", 2)) goto malformed;
    if (ru16(f, endian) != 42) goto malformed;
    
    long offset = ru32(f, endian);
    while(offset > 0) {
        fseek(f, offset, SEEK_SET);
        int ifd_count = ru16(f, endian);
        if (ifd_count < 0) goto malformed;
        for(int i=0; i<ifd_count; i+=1) {
            int tag = ru16(f, endian);
            int type = ru16(f, endian);
            if (type <= 0 || type > 12) goto malformed;
            unsigned count = ru32(f, endian);
            unsigned length = count * length_of_type[type];
            unsigned value = ru32(f, endian);
            if (tag == 256) {
                if (type == 3 && !endian) ans.width = (value>>16)&0xFFFF;
                else if (type == 3 && endian) ans.width = value&0xFFFF;
                else if (type == 4) ans.width = value;
                else {
                    fprintf(stderr, "Unexpected image width type %d\n", type);
                    goto malformed;
                }
            } else if (tag == 257) {
                if (type == 3 && !endian) ans.height = (value>>16)&0xFFFF;
                else if (type == 3 && endian) ans.height = value&0xFFFF;
                else if (type == 4) ans.height = value;
                else {
                    fprintf(stderr, "Unexpected image height type %d\n", type);
                    goto malformed;
                }
            } else if (tag == 700 && (type == 1 || type == 7) && length > 4) {
                long back = ftell(f);
                char *xmp = read_block(f, value, length);
                if (xmp) add_packet(&ans, xmp);
                fseek(f, back, SEEK_SET);
            }
        }
        offset = ru32(f, endian);
    }
    if (offset == 0) goto end;

malformed:
    if (ans.packets) {
        char **p = ans.packets;
        while (*p) { free(*p); *p=NULL; p+=1; }
        free(ans.packets);
        ans.packets = NULL;
    }
    ans.width = ans.height = 0;

end:
    fclose(f);
    return ans;
}
//////////////////////////////// TIFF ///////////////////////////////

//////////////////////////////// SVG ////////////////////////////////
//////////////////////////////// SVG ////////////////////////////////

/////////////////////////////// OTHER ///////////////////////////////
xmp_rdata xmp_from_other(const char *filename) {
    FILE *f = fopen(filename, "rb");
    xmp_rdata ans = {0, 0, 0, NULL};
    
    const char *magic = "W5M0MpCehiHzreSzNTczkc9d'?>";
    int midx = 0;
    while(magic[midx] && !feof(f)) {
        int c = getc(f);
        if (c == magic[midx]) midx += 1;
        else if (c == '"' && magic[midx] == '\'') midx += 1;
        else midx = 0;
    }
    if (!magic[midx]) {
        long start = ftell(f);
        magic = "<?xpacket end='w'?>";
        midx = 0;
        while(magic[midx] && !feof(f)) {
            int c = getc(f);
            if (c == magic[midx]) midx += 1;
            else if (c == '"' && magic[midx] == '\'') midx += 1;
            else if (c == 'r' && magic[midx] == 'w') midx += 1;
            else midx = 0;
        }
        long end = ftell(f) - 19;
        add_packet(&ans, read_block(f, start, end-start));
        ans.width = -1;
        ans.height = -1;
    }
    fclose(f);
    return ans;
}

int xmp_to_other(const char *ref, const char *dest, const char *xmp) {
    int fd = open(dest, O_WRONLY | O_EXCL | O_CREAT, 0644);
    if (fd < 0) return 0;
    FILE *f = fopen(ref, "rb");
    FILE *t = fdopen(fd, "wb");

    size_t needed = strlen(xmp);
    while (!feof(f)) {
        const char *magic = "W5M0MpCehiHzreSzNTczkc9d'?>";
        int midx = 0;
        while(magic[midx] && !feof(f)) {
            int c = getc(f);
            if (c == magic[midx]) midx += 1;
            else if (c == '"' && magic[midx] == '\'') midx += 1;
            else midx = 0;
        }
        if (!magic[midx]) {
            long start = ftell(f);
            magic = "<?xpacket end='w'?>";
            midx = 0;
            int ok = 1;
            while(magic[midx] && !feof(f)) {
                int c = getc(f);
                if (c == magic[midx]) midx += 1;
                else if (c == '"' && magic[midx] == '\'') midx += 1;
                else if (c == 'r' && magic[midx] == 'w') { midx += 1; ok = 0; }
                else midx = 0;
            }
            long end = ftell(f) - 19;
            if (ok && !magic[midx] && end-start >= needed) {
                fseek(f, 0, SEEK_SET);
                copy_bytes(f, t, start);
                fputs(xmp, t);
                for(size_t i=needed; i<end-start; i+=1)
                    putc((i%100) ? ' ' : '\n', t);

                fseek(f, 0, SEEK_END);
                long fsize = ftell(f);
                fseek(f, end, SEEK_SET);
                copy_bytes(f, t, fsize-end);
                
                goto end;
            }
        }
    }

    fclose(f);
    fclose(t);
    unlink(dest);
    return 0;

end:
    fclose(f);
    fclose(t);
    return 1;
}
/////////////////////////////// OTHER ///////////////////////////////

