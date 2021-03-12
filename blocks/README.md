XMP is stored in image files inside contiguous blocks of the file.
The contents of those blocks is RDF/XML, the parsing of which is discussed [elsewhere](../rdfxml).
This folder describes and provides example code for locating, inserting, and modifying the XMP blocks themselves.

Contents include

- Manual XMP block location and modification
    - [x] A [guide to doing this by hand](guide.md) with imperative-style pseudocode
    - [ ] A [reference C implementation](xmpblock.c) with [header](xmpblock.h) and [minimal example usage](xmpblock_example.c)
        - [x] AVIF, HEIC, JPEG2000
        - [x] GIF
        - [x] JPEG
        - [x] PNG
        - [ ] SVG
        - [x] TIFF (read only)
        - [x] WEBP
        - [x] Unknown
        - [x] apply `<?xpacket?>` wrappers and space padding
- [ ] Guides to doing this with command-line tools:
    - [ ] Exiftool
    - [ ] exiv2
    - [ ] ImageMagick
- [ ] Guides to doing this with various libraries
    - [ ] Adobe XMP Toolkit (C++)
    - [ ] exempi (C)
    - [ ] Exiftool (Perl)
    - [ ] others to be added ...

