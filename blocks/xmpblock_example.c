#include "xmpblock.h"
#include <stdio.h>

const char *xmp_to_write = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF  xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><rdf:Description rdf:about=""><dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Demo XMP content</rdf:li></rdf:Alt></dc:title></rdf:Description></rdf:RDF></x:xmpmeta>";

int main(int argc, char *argv[]) {
    for(int i=1; i<argc; i+=1) {
        xmp_rdata dat = xmp_from_gif(argv[i]);
        if (dat.width) {
            printf("GIF %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            if (xmp_to_gif(argv[i], "output.gif", xmp_to_write))
                printf("wrote output.gif\n");
            else
                printf("WARNING: output.gif already exists, not modified\n");
            continue;
        }
        
        dat = xmp_from_isobmf(argv[i]);
        if (dat.width) {
            printf("ISOBMF %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            if (xmp_to_isobmf(argv[i], "output.isobmf", xmp_to_write))
                printf("wrote output.isobmf\n");
            else
                printf("WARNING: output.isobmf already exists, not modified\n");
            continue;
        }

        dat = xmp_from_jpeg(argv[i]);
        if (dat.width) {
            printf("JPEG %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            if (xmp_to_jpeg(argv[i], "output.jpg", xmp_to_write))
                printf("wrote output.jpg\n");
            else
                printf("WARNING: output.jpg already exists, not modified\n");
            continue;
        }

        dat = xmp_from_png(argv[i]);
        if (dat.width) {
            printf("PNG %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            if (xmp_to_png(argv[i], "output.png", xmp_to_write))
                printf("wrote output.png\n");
            else
                printf("WARNING: output.png already exists, not modified\n");
            continue;
        }

        dat = xmp_from_webp(argv[i]);
        if (dat.width) {
            printf("WEBP %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            if (xmp_to_webp(argv[i], "output.webp", xmp_to_write))
                printf("wrote output.webp\n");
            else
                printf("WARNING: output.webp already exists, not modified\n");
            continue;
        }

        dat = xmp_from_tiff(argv[i]);
        if (dat.width) {
            printf("TIFF %s: %u×%u with %lu packets\n", argv[i], dat.width, dat.height, dat.num_packets);
            for(int i=0; i<dat.num_packets; i+=1) {
                puts(dat.packets[i]);
            }
            continue;
        }
    }
}
