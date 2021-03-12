---
title: Finding XMP in image files
author: Luther Tychonievich
...


# Preliminaries

The purpose of this document is to provide practical guidelines for implementers who wish to find and edit the XMP packets in various image file formats.

## Scope

It is my intent to cover image formats I anticipate appearing in online family history software tools; at present time, that includes:

- Web formats: JPEG, PNG, WebP, and GIF
- Camera formats: TIFF and DNG
- Formats some hope will replace the above: AVIF, HEIC, and JPEG2000

I may at some point add more image formats. JEPG XL is likely to be added once it is released and I have time to learn how it works; BMP, FLIF, JEPG XR, Targa, and others are less likely.

While XMP can also be used in video, audio, and paged media formats, I do not intend to add those to this document.

## Sources

The following sources were consulted in creating this document:

- Exif specification (<http://www.cipa.jp/std/documents/e/DC-008-2012_E.pdf>)
- GIF specification (<https://www.w3.org/Graphics/GIF/spec-gif89a.txt>)
- JFIF specification (<https://www.w3.org/Graphics/JPEG/jfif3.pdf>)
- JPEG specification (<http://www.w3.org/Graphics/JPEG/itu-t81.pdf>)
- PNG specification (<http://www.w3.org/TR/PNG>)
- WEBP container specification (<https://developers.google.com/speed/webp/docs/riff_container>)
- VP8L specification (<https://developers.google.com/speed/webp/docs/webp_lossless_bitstream_specification>)
- VP8 specification (<tools.ietf.org/html/rfc6386>)
- XMP Specification parts 1 and 3 (<https://www.adobe.com/devnet/xmp.html>)
- Inspection of example files with and without XMP included

You will note that specifications for JPEG2000 and HEIC are not in this list. Those standards have been closed to the public behind ISO and IEC pay-walls. The basic information in XMP part 3 was enough to analyze example files and inform the code described here, but it is possible I have overlooked something the standards describe that is not obvious by inspecting files.

I have not consulted any other XMP reading or writing code base or technical guide, either as part of this project or prior to it. Accordingly, I may be missing some known prior art. The intent is that writing this without consulting other sources will avoid copyright and copyleft issues.

## XMP Padding

The XMP specification recommends that each XMP packet end with 2KB of spaces and newlines. The intent of this appears to be to allow some in-place editing of XMP inside a file.

In-place editing does have value in some circumstances. It can avoid the overhead of moving later bytes in large files like video, but that cost in image files is generally quite low.
It can allow some forms of XMP editing by software that does not understand the particular file format (see the discussion in section [Other]) but in my informal experience, tools either edit XMP properly or not at all; I've not noticed any that can only operate if padding is available for them to work with.
Additionally, some files formats have extra checksums or the like that render format-unaware editing impossible even with the padding.

This document describes writing XMP as copying all non-XMP data and adding in XMP data. This should work for all files in the described formats, regardless of padding and previous XMP length, as long as the input and output files are distinct. Except in rare cases, it will not work in-place. [Other] describes an alternative approach for in-place editing and techniques for determining if it is supported by the file in question.

## Image dimensions

In addition to the XMP packet, the reading code also tries to find the pixel dimensions of the image. This is only needed if you want to convert between relative and pixel image regions; if that's not something you plan to do, feel free to ignore the dimension-locating code.

## Pseudocode syntax in this document

This document is written using a custom procedural pseudocode;
the hope it is will translate naturally into many languages.

Datatypes are represented as a letter, a bit width, and an optional array length.
The letters used as `u` for unsigned integer, `c` for character, and `b` for uninterpreted binary.
Literal values are shown as decimal integers for `u`-types, C-style strings in quotes for `c`-types, and upper-case two-digit hexadecimal values for `b`-types.

C-style binary operators are assumed: `&` for bitwise [and]{.smallcaps}, `|` for bitwise [or]{.smallcaps}, `<<` for left bit shift.

Fixed-length arrays are denoted with their element type followed by the number of elements in brackets. For example `u24[10]` is a 30-byte value consisting of 10 3-byte unsigned integers.
Variable-length arrays are denoted by their element type followed by empty brackets, like `c8[]`.

In all cases when reading, it is possible to encounter multiple distinct XMP packets. These are not documented in the XMP specification (except in one special instance for JPEG), but are present in the wild. The one documented use of multiple packets is JPEG, where the first packet has a size limit and a second packet is allowed to overcome that limit; per XMP, the two XMP packets in JPEG should be merged. I assume repeated packets created by non-standards-complaint tools should also be merged, but have no way to test that intent.

# GIF

GIF files are little-endian.

GIF has a system for encoding variable-length payloads, but XMP ignores it because it intersperses extra bytes and instead uses a fancy "magic trailer." GIF does not guarantee the boundaries between sub-blocks will be preserved, so it is possible that the XMP packet will be corrupted beyond repair by a non-XMP-aware tool, but I can't find reference to this actually happening in the wild.

## Reading

1. read `c8[6]` *header*
1.  __if__ *header* = `"GIF87a"`
    1. read `u16` **width**
    1. read `u16` **height**
    1. terminate processing (cannot store XMP)
    
    __else if__ *header* = `GIF89a`
    1. read `u16` **width**
    1. read `u16` **height**
    1. read `u8` *flags*
    1. skip 2 bytes
    1.  __if__ `flags & 0x80` = `0x80`
        1. skip `6 << (flags & 7)` bytes
    
    __else__
    1. not a GIF file
1. __repeat__
    1. read `b8` *intro*
    1.  __if__ *intro* = `3B`
        1. terminate processing (reached end of image)
        
        __else if__ *intro* = `2C`
        1. skip 8 bytes
        1. read `u8` *flags*
        1.  __if__ `flags & 0x80` = `0x80`
            1. skip `6 << (flags & 7)` bytes
        1. skip 1 byte
        1. read `u8` *length*
        1. __while__ *length* ≠ 0
            1. skip *length* bytes
            1. read `u8` *length*
        
        __else if__ *intro* = `21`
        1. read `b8` *label*
        1. __if__ *label* = `FF`
            1. read `u8` equal to 11
            1. read `c8[11]` *appid*
            1. __if__ *appid* = `"XMP DataXMP"`
                1. read bytes into **xmp_data** up to (but not including) first `01` byte
                1. read `b8[258]` equal to `01` `FF` `FE` `FD` ... `03` `02` `01` `00` `00`
                
                __else__
                1. read `u8` *length*
                1. __while__ *length* ≠ 0
                    1. skip *length* bytes
                    1. read `u8` *length*

            __else__
            1. read `u8` *length*
            1. __while__ *length* ≠ 0
                1. skip *length* bytes
                1. read `u8` *length*
        
        __else__
        1. malformed GIF file

## Writing

The XMP trailer should have `end="w"` and may benefit from padding.

1. skip 6 bytes
1. write `c8[6]` `"GIF89a"`
1. copy 4 bytes
1. copy `u8` *flags*
1. copy 2 bytes
1.  __if__ `flags & 0x80` = `0x80`
    1. copy `6 << (flags & 7)` bytes
1. __repeat__
    1. read `b8` *intro*
    1.  __if__ *intro* = `3B`
        1.  __if__ have written XMP
            1. write `b8` *intro*
            1. terminate processing (reached end of image)
            
            __else__
            1. write `u8[2]` `21` `FF`
            1. write `c8[11]` `"XMP DataXMP"`
            1. write **xmp_data**
            1. write `b8[258]` `01` `FF` `FE` `FD` ... `03` `02` `01` `00` `00`
            1. write `b8` *intro*
            1. terminate processing (reached end of image)
        
        __else if__ *intro* = `2C`
        1. write `b8` *intro*
        1. copy 8 bytes
        1. copy `u8` *flags*
        1.  __if__ `flags & 0x80` = `0x80`
            1. copy `6 << (flags & 7)` bytes
        1. copy 1 byte
        1. copy `u8` *length*
        1. __while__ *length* ≠ 0
            1. copy *length* bytes
            1. copy `u8` *length*
    
        __else if__ *intro* = `21`
        1. read `b8` *label*
        1. __if__ *label* = `FF`
            1. read `u8` equal to 11
            1. read `c8[11]` *appid*
            1. __if__ *appid* = `"XMP DataXMP"` and have written XMP
                1. skip bytes until byte `01` skipped
                1. skip 257 bytes
            
                __else if__ *appid* = `"XMP DataXMP"`
                1. write `b8` *intro*
                1. write `b8` *label*
                1. write `u8` 11
                1. wriate `c8[11]` *appid*
                1. skip bytes until byte `01` skipped
                1. write **xmp_data**
                1. write `b8` `01`
                1. copy 257 bytes

                __else__
                1. write `b8` *intro*
                1. write `b8` *label*
                1. copy `u8` *length*
                1. __while__ *length* ≠ 0
                    1. copy *length* bytes
                    1. copy `u8` *length*

            __else__
            1. write `b8` *intro*
            1. write `b8` *label*
            1. copy `u8` *length*
            1. __while__ *length* ≠ 0
                1. copy *length* bytes
                1. copy `u8` *length*
    
        __else__
        1.   malformed GIF file


# ISOBMF (.jp2, .heic, .avif)

ISOBMF files are big-endian.

The ISO Base Media Format is used by JPEG2000 (.jp2), HEIF (.heic), and AVIF (.avif) files. It is also used by several video formats.

I have not yet found a public description of the details of AVIF; the AVIF code here is based on inspection of avif files and may be missing some nuance.

Image dimensions are in

- jp2h.ihdr (JPEG 2000)
- meta.idat (HEIC)
- meta.iprp.ipco.ispe (AVIF)

## Reading

To get the size of the image requires a recursive box reading, so we define a box-read subroutine as well as a file-read routine

1. read a box
1.  __if__ *type* = `"jp  "` and *data* = `"\r\n\x87\n"`
    1. this is a JPEG2000
    
    __else if__ *type* = `"ftyp"` and `"heic"` appears on a 4-byte-aligned boundary after the first 8 bytes of *data*
    1. this is an HEIC

    __else if__ *type* = `"ftyp"` and `"avif"` appears on a 4-byte-aligned boundary after the first 8 bytes of *data*
    1. this is an AVIF

    __else__ 
    1. this is some other file type

1. __while__ not at the end of the file
    1. read a box
    1.  __if__ this is a JPEG200 and *type* = `"jp2h"`
        1.   __while__ not at end of *data*
            1.  read a box from *data* as *inner*
            1.  __if__ *inner*.*type* = `"ihdr"`
                1. read `u32` **height** from *inner*.*data*
                1. read `u32` **width** from *inner*.*data*
        
        __else if__ this is a HEIC or AVIF and *type* = `"meta"`
            1.  skip 4 bytes
            
                > I can't find these 4 bytes documented, but test files I tried had them, all equal to 0
            1.  __while__ not at end of *data*
                1.  read a box from *data* as *inner*
                1.  __if__ HEIC and *inner*.*type* = `"idat"`
                    1. skip 4 bytes of *inner*.*data*
                    1. read `u16` **width** from *inner*.*data*
                    1. read `u16` **height** from *inner*.*data*
                
                    __else if__ AVIF and *inner*.*type* = `"iprp"`
                    1. read boxes from *inner*.*data* until find one with *type* = `"ipco"`
                    1. read boxes from ipco's *data* until find one with *type* = `"ispe"`
                    1. skip 4 bytes of ispe's data
                    1. read `u32` **width** from *inner*.*data*
                    1. read `u32` **height** from *inner*.*data*

        __else if__ *type* = `"uuid"` and first 16 bytes of *data* are `BE` `7A` `CF` `CB` `97` `A9` `42` `E8` `9C` `71` `99` `94` `91` `E3` `AF` `AC` 
        1.  all but first 16 bytes of *data* are **xmp_packet**


where "read a box" means

1. read `u32` *length*
1. read `c8[4]` **type**
1.  __if__ *length* = 1
    1. read `u64` replacing *length*
    1. subtract 8 from *length*
    
    __else if__ *length* = 0
    1. replace *length* with 8 + number of unread bytes remaining
1.  __if__ *length* < 8
    1. malformed ISOBMF file
1.  read `b8[length-8]` **data**

It is generally not necessary to actually read the *data* into memory in the last step above; most *data* is either not used or used only as a region for reading additional boxes, so noting its bounds (and seeking past its end) is often sufficient.

## Writing

The XMP trailer should have `end="w"` and may benefit from padding.

> note: this code puts the XMP packet at the first location of
>
> - the first location of an XMP packet in the reference file
> - right before the "rest of file" box (if there is one)
> - the end of the file
>
> I did this because there did not seem to be a different recommendation in the XMP, HEIF, or JPEG200 specifications and because the "rest of file" box seemed to be used consistently to place a particular box type at the end of some file formats.


1. __while__ not at end of file
    1. read `u32` *length~1~*
    1. read `c8[4]` **type**
    1.  __if__ *length~1~* = 1
        1. read `u64` *length~2~*
        
        __else if__ *length~1~* = 0
        1. write xmp packet
        1. let *length~2~* = 16 + number of unread bytes remaining

        __else__
        1. let *length~2~* = *length~1~* + 8
    1.  __if__ *type* = `"uuid"`
        1.  read `b8[16]` *uuid*
        1.  __if__ *uuid* = `BE` `7A` `CF` `CB` `97` `A9` `42` `E8` `9C` `71` `99` `94` `91` `E3` `AF` `AC`
            1. write xmp packet
            1. skip *length~2~* − 32 bytes
                
            __else__
            1. write `u32` *length~1~*
            1. write `c8[4]` *type*
            1. __if__ *length~1~* = 1
                1.   write `u64` *length~2~*
            1. write `b8[16]` *uuid*
            1. copy *length~2~* − 32 bytes

        __else__
        1. write `u32` *length~1~*
        1. write `c8[4]` *type*
        1. __if__ *length~1~* = 1
            1. write `u64` *length~2~*
        1. copy *length~2~* − 16 bytes
1. write xmp packet

where "write xmp packet" means

__if__ have not yet written **xmp_packet**
1. __if__ 24 + length of **xmp_data** ≥ 2^32^
    1. write `u32` 1
    1. write `c8[4]` `"uuid"`
    1. write `u64` 30 + length of **xmp_data**
    
    __else__
    1. write `u32` 24 + length of **xmp_data**
    1. write `c8[4]` `"uuid"`
1. write `b8[16]` `BE` `7A` `CF` `CB` `97` `A9` `42` `E8` `9C` `71` `99` `94` `91` `E3` `AF` `AC`
1. write **xmp_packet**



# JPEG

JPEG files are big-endian.

JPEG files have several formats, notably JIF, JFIF, and Exif.
These differ primarily in the defined order of segments.
For XMP, all can be treated equivalently.
Getting width and height does depend on internal format.

JPEG has complicated systems to deal with XMP datasets that serialize as larger than 64KiB:

- A standard XMP packet is included, called **xmp_packet** below
- That packet may include a special value, `xmpNote:HasExtendedXMP` in namespace `http://ns.adobe.com/xmp/note/` whose payload is an MD5 checksum as an upper-case hexadecimal string.
- The file may have any number of slices of extended XMP, each labeled with the MD5 checksum and entire length of the extended XMP serialization it belongs to and its offset within that serialization.
- The full XMP data is the union of that given in the standard packet
    and that found in the assembled extended XMP  `xmpNote:HasExtendedXMP`


## Reading

1. read `b8[2]` equal to `FF` `D8`
1. read `b8` *m~0~*
1. __while__ not at end of file
    1. read `b8`  *m~1~*
    1. __if__ *m~0~* = `FF` and *m~1~* = `E1`
        1.  read `u16` *length*
        1.  read `c8[]` *namespace*, stopping after the first `00` byte or *length* − 2 bytes, whichever comes first
        1.  __if__ *namespace* = `"http://ns.adobe.com/xap/1.0/\0"`
            1. read `c8[length-31]` **xmp_packet**
                    
            __else if__ *namespace* = `"http://ns.adobe.com/xmp/extension/\0"`
            1. read `c8[32]` *guid*
            1. read *u32* *extended_length*
            1. read *u32* *extended_offset*
            1. read `c8[length-77]` and insert into extended XMP with label *guid* at index *extended_offset*
                    
            __else__
            1. skip *length* − 2 − length of *namespace* bytes

        __else if__ *m~0~* = `FF` and `C0` ≤ *m~1~* ≤ `CF` and *m~1~* ≠ `C4` or `CC`
        1. skip 3 bytes
        1. read `u16` **height**, keeping old **height** if it was larger
        1. read `u16` **width**, keeping old **width** if it was larger
        
        > Keeping larger is due to embedded thumbnails

        __else if__ *m~0~* = `FF` and *m~1~* = `DC`
        1. skip 2 bytes
        1. read `u16` **height**, keeping old **height** if it was larger
    1. change *m~0~* to equal *m~1~*
1. __if__ **xmp_packet** was read<br/> **and if** **xmp_packet** has a field `xmpNote:HasExtendedXMP`<br/> **and if** the value of `xmpNote:HasExtendedXMP` is the label of an extended XMP<br/> **and if** the MD5 checksum of that extended XMP equals its label
    1. add all metadata from that extended XMP to **xmp_packet**

## Writing

JPEG cannot store a single XMP packet of more than 65502 bytes.

If more data is present, it needs to be split into exactly two packets:
one with all the most-important data no more than 65502 bytes including the packet header; and one of any size containing all the other data. The first, called **xmp_packet** below, shall contain a field `xmpNote:HasExtendedXMP` whose value is the upper-case hexadecial MD5 checksum of the second, called **extended_xmp** below.

The process of splitting XMP datasets is beyond the scope of this document.

The XMP trailer of **xmp_packet** should have `end="w"` and may benefit from padding.
No XMP header, trailer, or padding should be used for **extended_xmp**.


> The various specifications are inconsistent in the ordering of segments. This process places it in the first encountered spot of 
> 
> - current location of APP1 XMP packet
> - immediately before APP13 PSIR/IPTC
> - immediately before SOF
> 
> So far as I can tell, this will satisfy all specifications, including systems that try to mix several by having multiple variants' "must be first" segments at the front.

1. copy `b8[2]` equal to `FF` `D8`
1. read `b8` *m~0~*
1. __while__ not at end of file
    1. read `b8`  *m~1~*
    1. __if__ *m~0~* = `FF` and *m~1~* = `E1`
        1. read `u16` *length*
        1.  read `c8[]` *namespace*, stopping after the first `00` byte or *length* − 2 bytes, whichever comes first
        1.  __if__ *namespace* = `"http://ns.adobe.com/xap/1.0/\0”`
            1.  write xmp data
            1.  skip *length* − 31 bytes

            __else if__ *namespace* = `"http://ns.adobe.com/xmp/extension/"\0”`
            1. skip *length* − 37 bytes
                            
            __else__ 
            1. write `b8` *m~0~*
            1. write `b8` *m~1~*
            1. write `u16` *length*
            1. write *namespace*
            1. copy *length* − 2 − length of *namespace* bytes
        1. read `b8`  *m~1~*
        
        __else if__ *m~0~* = `FF` and *m~1~* = `ED`
        1. read `u16` *length*
        1.  read `c8[]` *namespace*, stopping after the first `00` byte or *length* − 2 bytes, whichever comes first
        1.  __if__ *namespace* = `"Photoshop 3.0\0”`
            1. write xmp data
        1. write `b8` *m~0~*
        1. write `b8` *m~1~*
        1. write `u16` *length*
        1. write *namespace*
        1. copy *length* − 2 − length of *namespace* bytes
        1. read `b8`  *m~1~*
            
        __else if__ *m~0~* = `FF` and `C0` ≤ *m~1~* ≤ `CF` and *m~1~* ≠ `C4` or `CC`
        1. write xmp data
        1. write `b8` *m~0~*
        
        __else__
        1. write `b8` *m~0~*

    1. change *m~0~* to equal *m~1~*

where "write xmp data" means

__if__ have not written xmp data yet
1. write `b8[2]` `FF` `E1`
1. write `u16` length of **xmp_data** + 31
1. write `c8[29]` `"http://ns.adobe.com/xap/1.0/\0”`
1. write **xmp_data**
1. __if__ there is an **extended_xmp**
    1. split the bytes of **extended_xmp** into chunks of no more than 65498 bytes each
    1. **for each** *chunk*
        1. write `b8[2]` `FF` `E1`
        1. write `u16` length of *chunk* + 37
        1. write `c8[35]` `"http://ns.adobe.com/xmp/extension/\0”`
        1. write *chunk*
    


# PNG

PNG files are big-endian.

PNG requires access to CRC32 subroutine, as defind in ISO 3309 and ITU-T V.42. A reference C implementation is provided as part of the PNG spec <https://www.w3.org/TR/PNG/#D-CRCAppendix>

## Reading

1. read `c8[8]` equal to `"\x89PNG\r\n\x1a\n"`
1. read the header as follows:
    1. read `u32` equal to 13
    1. read `c8[4]` equal to `"IHDR"`
    1. read `u32` **width**
    1. read `u32` **height**
    1. skip 5 bytes
    1. read `u32` *crc*
    1. optionally, verify that *crc* = CRC32(previous 17 bytes)
1. __while__ not at end of file, read a chunk as
    1. read `u32` *length*
    1. read `c8[4]` *type*
    1.  __if__ *type* is `"iTXt"` and *length* > 22
        1. read `c8[22]` *label*
        
        1.  __if__ *label* is `"XML:com.adobe.xmp\0\0\0\0\0"`
            1.   read `c8[length-22]` **xmp_packet**

            __else__ 
            1.   skip *length* − 22 bytes
    
        __else__ 
        1.   skip *length* bytes
    1. read `u32` *crc*
    1. optionally, verify that *crc* = CRC32(bytes of chunk excluding *length* and *crc*)

## Writing 

The XMP trailer must have `end="r"` and does not benefit from padding.

1. copy 33 bytes
1. write `u32` length of **xmp_packet** + 22
1. write `c8[4]` `"iTXt"`
1. write `c8[22]` `"XML:com.adobe.xmp\0\0\0\0\0"`
1. write **xmp_packet**
1. write CRC32(`"iTXt"`, `"XML:com.adobe.xmp\0\0\0\0\0"`, **xmp_packet**)
1. __while__ not at end of file, read a chunk as
    1. read `u32` *length*
    1. read `c8[4]` *type*
    1.  __if__ *type* is `"iTXt"` and *length* ≥ 22
        1. read `c8[22]` *label*
        
        1.  __if__ *label* is `"XML:com.adobe.xmp\0\0\0\0\0"`
            1. skip *length* − 18 bytes
            
            __else__ 
            1. write `u32` *length*
            1. write `c8[4]` *type*
            1. write `c8[22]` *label*
            1. copy *length* − 18 bytes
    
        __else__ 
        1. write `u32` *length*
        1. write `c8[4]` *type*
        1. copy *length* + 4 bytes



# SVG

SVG is an XML document, which means it might be in any character encoding. It also has a compressed form SVGZ which requires a compression library to read and write. This guide describes it in terms of code points

SVG also has the potential for several different "pixel" units at the same time: the viewbox, width, and display-width. This guide does not suggest how to handle these.

Additionally, two metadata fields (`dc:title` and `dc:description`) are stored in the XML directly and supersede those in the metadata block.

Because XMP is stored as RDF/XML, I assume any applicaiton will have XML processing in place.

## Read/Write as As XML

1. `dc:title` is the text node inside the (unique) `/svg/title` element, unless that element contains sub-elements
1. `dc:description` is the text node inside the (unique) `/svg/desc` element, unless that element contains sub-elements
1. all other XMP metadata is placed inside the `/svg/metadata/x:xmpmeta` element, of which there may be several but should only be one
    
    Note that SVG does not include the `<?xpacket ...?>` wrappers that most other files do; the `x:xmpmeta` root element is used directly.


# TIFF (.tif, .tiff, .dng)

TIFF's endianness is determined by its first two bytes.

TIFF is organized by a linked-list of arrays of tags. The arrays have to be sorted and "important" data has to be in the first. The entries in the arrays are fixed width and often point to external payloads. All pointers are absolute addresses, so data cannot be inserted or removed from the middle without changing a lot of pointers, potentially including some in unknown extension blocks. Because of this, TIFF is effectively a read-only file format.

## Reading

1. read `c8[2]` *endian*
1. __if__ *endian* = `"II"`
    1. file is little-endian
    
    __else if__ *endian* = `"MM"`
    1.  file is big-endian
    
    __else__
    1.  file is not TIFF
1. read `u16` equal to 42
1. read `u32` *offset*
1. __while__ *offset* ≠ 0
    1. seek to file position *offset* 
    1. read `u16` *count*
    1. __repeat__ *count* times
        1. read `u16` *tag*
        1. read `u16` *type*
        1. __if__ *type* = 0 or *type* > 12
            1.   malformed TIFF
        1. read `u32` *length*
        1. read `u32` *value*
        1.  __if__ *tag* = 256
            1.  __if__ *type* = 3 and little-endian
                1. let **width** = `value & 0xFFFF`
                
                __else if__ *type* = 3 and big-endian
                1. let **width** = `value >> 16`
                
                __else if__ *type* = 4
                1. let **width** = *value*
                
            __else if__ *tag* = 257
            1.   __if__ *type* = 3 and little-endian
                1. let **height** = `value & 0xFFFF`
                
                __else if__ *type* = 3 and big-endian
                1. let **height** = `value >> 16`
                
                __else if__ *type* = 4
                1. let **height** = *value*

            __else if__ *tag* = 700 and (*type* = 1 or *type* = 7)
            1. let *back* be current file position
            1. seek to *value*
            1. read `c8[length]` **xmp_packet**
            1. seek to *back*

    1. read `u32` *offset*

# WEBP

WEBP files are technically a type of RIFF, but other uses of RIFF (mostly used for audio files) follow a different process for storing XMP than WEBP.

WEBP is also three formats: two unrelated formats (VP8 and VP8L), either of which may appear on its own or in third wrapper format (VP8X). Only the wrapper ever has metadata. Hence, the writing code will add the wrapper if it is not present.

## Reading

1. read `c8[4]` equal to `"RIFF"`
1. read `u32` equal to file size − 8
1. read `c8[4]` equal to `"WEBP"`
1. read `c8[4]` *variant*
1. read `u32` *length*
1.  __if__ *variant* = `"VP8 "`
    1. skip 6 bytes
    1. read `u16` **width**
    1. read `u16` **height**
    1. terminate processing (no XMP in this format)
    
    __else if__ *variant* = `"VP8L"`
    1. read `b8` equal to `2F`
    1. read `u32` *packed*
    1. let **width** = `1 + (packed & 0x3FFF)`
    1. let **height** = `1 + ((packed>>14) & 0x3FFF)`
    1. terminate processing (no XMP in this format)
    
    __else if__ *variant* = `"VP8X"`
    1. skip 4 bytes
    1. read `u24` and add 1 to get **width**
    1. read `u24` and add 1 to get **height**
    1. skip *length* − 10 bytes
    1. __if__ *length* is odd
        1. skip 1 byte
    
    __else__
    1.  malformed WEBP

1. __while__ not at end of file, read a chunk as
    1. read `c8[4]` *fourcc*
    1. read `u32` *length*
    1.  __if__ *forcc* = `"XMP "`
        1. read `c8[length]` **xmp_packet**
        
        __else__
        1.  skip *length* bytes
    1. if *length* is odd, skip 1 byte

## Writing

The XMP trailer should have `end="w"` and may benefit from padding.


1. copy `c8[4]` equal to `"RIFF"`
1. copy `u32` equal to file size − 8
1. copy `c8[4]` equal to `"WEBP"`
1. read `c8[4]` *variant*
1. read `u32` *length*
1.  __if__ *variant* = `"VP8 "`
    1. read VP8 data needed in VP8X by
        1. read `b8[6]` *prefix*
        1. read `u16` **width**
        1. read `u16` **height**
    1. write a VP8X header
    1. copy VP8 block by
        1. write `c8[4]` *variant*
        1. write `u32` *length*
        1. write `b8[6]` *prefix*
        1. write `u16` **width**
        1. write `u16` **height**
        1. copy *length* − 10 bytes
    1. __if__ *length* is odd
        1. copy 1 byte
    
    __else if__ *variant* = `"VP8L"`
    1. read VP8L data needed in VP8X by
        1. read `b8` equal to `2F`
        1. read `u32` *packed*
        1. let **width** = `1 + (packed & 0x3FFF)`
        1. let **height** = `1 + ((packed>>14) & 0x3FFF)`
        1. let *alpha* = `(packed >> 28) & 1`
    1. write a VP8X header
    1. copy VP8L block by
        1. write `c8[4]` *variant*
        1. write `u32` *length*
        1. write `b8` `2D`
        1. write `u32` *packed*
        1. copy *length* − 5 bytes
    1. __if__ *length* is odd
        1. copy 1 byte
    
    __else if__ *variant* = `"VP8X"`
    1. write `c8[4]` *variant*
    1. write `u32` *length*
    1. read `b8` *flags*
    1. write `b8` `flags | 4`
    1. copy *length* − 1 bytes
    1. __if__ *length* is odd
        1.  copy 1 byte
    1. __while__ not at end of file
        1. read `c8[4]` *fourcc*
        1. read `u32` *length*
        1.  __if__ *forcc* = `"XMP "`
            1. skip *length* bytes
            
            __else__
            1. write `c8[4]` *fourcc*
            1. write `u32` *length*
            1. copy *length* bytes
            1. __if__ *length* is odd
                1. copy 1 byte

    __else__
    1. malformed WEBP
1. write XMP chunk by
    1. write `c8[4]` `"XMP "`
    1. write `u32` length of **xmp_packet**
    1. write **xmp_packet**
    1. __if__ length of **xmp_packet** is odd
        1.  write `u8` `00`
1. store current file position as *length*
1. seek back to file offset 4
1. write `u32` *length* − 8 over the bytes currently there

where "write VP8X header" means

1. write `c8[4]` `"VP8X"`
1. write `u32` 10
1. __if__ *alpha* has be set to 1
    1. write `b8` `14`
    
    __else__
    1. write `b8` `04`
1. write `u24` 0
1. write `u24` **width** − 1
1. write `u24` **height** − 1


# Other

The XMP spec has jumped through hoops to enable XMP reading (and in some cases writing) even in unknown file formats.

If a file contains contiguous XMP that is delimited as follows:

- Start `<?xpacket begin="`BOM`" id="W5M0MpCehiHzreSzNTczkc9d"?>`
    -   BOM is the byte order mark character (U+FEFF, which encodes as `EF` `BB` `BF` in UTF-8) and is optional
    -   single-quotes may be used instead of double-quotes
    -   extra characters may be inserted between the final quote and the closing question mark

- End `<?xpacket end="w"?>`
    -   `r` may be used instead of `w`
    -   single-quotes may be used instead of double-quotes

then the text between this pair is XMP, in the encoding indicated by the byte order mark if present or UTF-8 if not, and can be read as such.
If the end marker used `w`, the XMP packet can safely be overwritten by any XMP packet of exactly the same number of bytes; white-space padding can be used to keep byte length the same.

A file may contain more than one XMP block delimited as above. XMP data may be split between them freely, provided each contains a complete RDF/XML document.

