/**
 * Locates the XMP packets  in a variety of file formats.
 * Currently supported are
 * 
 * <ul>
 * <li>GIF (87 and 89)</li>
 * <li>PNG</li>
 * <li>WEBP (VP8, VP8L, and VP8X)</li>
 * <li>JPEG (JFIF and Exif)</li>
 * <li>TIFF</li>
 * <li>ISO/IEC's family of image formats: (AVIF, HEIC, and JPEG2000)</li>
 * <li>any other with properly-formatted xpacket headers (no width or height extraction possible)</li>
 * </ul>
 * 
 * Note that the SVG spec includes XMP packets, but this file does not (yet) support SVG.
 * 
 * <p>
 * This implementation makes no particular effort to be efficient
 * or to conform to Java's recommended style, instead using a
 * procedural approach that can be mirrored in non-OO languages like C.
 * <p>
 * JPEG parsing in particular is implemented by reading 2 bytes at
 * a time, which with Java's <code>RandomAccessFile</code> is
 * very inefficient.
 * 
 * <p>
 * This is file is in the public domain and comes with NO WARRANTY
 * of any kind.
 */

import java.util.List;
import java.io.RandomAccessFile;
import java.io.IOException;

/**
 * <code>new XMPBlockReader("path/to/imagefile.end")</code>
 * will parse a file and initialize
 * 
 * <ul>
 * <li><code>format</code> is a string name of the file format
 *   (or <code>null</code> if file type unrecognized).</li>
 * <li><code>width</code> and <code>height</code> are in pixels (if recognized, 0 otherwise).</li>
 * <li><code>packets</code>, a <code>java.util.List</code> containing one <code>byte[]</code> for each XMP packet found.</li>
 * </ul>
 */
public class XMPBlockReader {
    public int width, height;
    public List<byte[]> packets;
    public String format, filename;
    
    private static final java.nio.charset.Charset UTF8 = java.nio.charset.Charset.forName("UTF-8");
    
    private RandomAccessFile f;
    private boolean littleendian;

    public XMPBlockReader(String filename) throws IOException {
        this.filename = filename;
        f = new RandomAccessFile(filename, "r");
        packets = new java.util.LinkedList<byte[]>();

        if (loadPNG()) { f.close(); f = null; return; } else f.seek(0);
        if (loadWEBP()) { f.close(); f = null; return; } else f.seek(0);
        if (loadGIF()) { f.close(); f = null; return; } else f.seek(0);
        if (loadJPEG()) { f.close(); f = null; return; } else f.seek(0);
        if (loadISOBMF()) { f.close(); f = null; return; } else f.seek(0);
        if (loadTIFF()) { f.close(); f = null; return; } else f.seek(0);
        if (loadOther()) { f.close(); f = null; return; } else f.seek(0);
        format = null;

        f.close(); f = null;
    }

    //////////////////////////// HELPERS ////////////////////////////
    private int ru8() throws IOException { return f.read(); }
    private int ru16() throws IOException {
        if (littleendian) return f.read() | (f.read()<<8);
        else return (f.read()<<8) | f.read();
    }
    private int ru24() throws IOException {
        if (littleendian) return f.read() | (f.read()<<8) | (f.read()<<16);
        else return (f.read()<<16) | (f.read()<<8) | f.read();
    }
    private long ru32() throws IOException {
        if (littleendian) return f.read() | (f.read()<<8) | (f.read()<<16) | (f.read()<<24);
        else return (f.read()<<24) | (f.read()<<16) | (f.read()<<8) | f.read();
    }
    private long ru64() throws IOException {
        if (littleendian) return ru32() | (ru32()<<32);
        else return (ru32()<<32) | ru32();
    }
    private static boolean memcmp(byte[] buf, String a, int len) {
        if (a.length() != len) return true;
        for(int i=0; i<len; i+=1) {
            if (buf[i] != a.charAt(i)) return true;
        }
        return false;
    }
    private static boolean memcmp(byte[] buf, int offset, String a, int len) {
        if (a.length() != len) return true;
        for(int i=0; i<len; i+=1) {
            if (buf[i+offset] != a.charAt(i)) return true;
        }
        return false;
    }
    private static int strstr(byte[] haystack, byte[] needle) {
        int ni = 0;
        for(int hi=0; hi<haystack.length; hi+=1) {
            if (haystack[hi] == needle[ni]) {
                ni += 1;
                if (ni == needle.length) return hi+1-ni;
            } else {
                hi -= ni;
                ni = 0;
            }
        }
        return -1;
    }
    //////////////////////////// HELPERS ////////////////////////////


    //////////////////////////// WRAPPING ///////////////////////////
    private byte[] read_block(long fpos, long size) throws IOException {
        byte[] buf = new byte[256];
        long start = fpos, end = fpos + size;

        // skip leading whitespace
        f.seek(start);
        while (Character.isWhitespace(ru8()) && start < end) start += 1;

        // if present, skip xpacket header (even if malformed)
        f.seek(start);
        f.read(buf, 0, 16);
        if (!memcmp(buf, "<?xpacket begin=", 16)) {
            while (ru8() != '?') {}
            if (ru8() != '>') return null;
            start = f.getFilePointer();
            // and whitespace after it
            while (Character.isWhitespace(ru8()) && start < end) start += 1;
        }

        // skip trailing whitespace
        int i = -1;
        while(i < 0 && end > start) {
            f.seek(end - buf.length);
            f.read(buf);
            for(i=buf.length-1; i>=0 && Character.isWhitespace(buf[i]); i-=1)
                end -= 1;
        }
        // if present, skip xpacket footer (even if malformed)
        f.seek(end-19);
        f.read(buf, 0, 19);
        if (!memcmp(buf, "<?xpacket end=", 14) && !memcmp(buf, 17, "?>", 2)) {
            end -= 19;
            // skip more trailing whitespace
            i = -1;
            while(i < 0 && end > start) {
                f.seek(end-buf.length);
                f.read(buf, 0, buf.length);
                for(i=buf.length-1; i>=0 && Character.isWhitespace(buf[i]); i-=1)
                    end -= 1;
            }
        }
        if (end > start) {
            byte[] ans = new byte[(int)(end-start)];
            f.seek(start);
            f.read(ans, 0, (int)(end-start));
            f.seek(fpos + size);
            return ans;
        } else {
            f.seek(fpos + size);
            return null;
        }
    }
    byte[] read_block_delim(long fpos, int delim) throws IOException {
        f.seek(fpos);
        while (ru8() != delim && f.getFilePointer() < f.length()) {}
        return read_block(fpos, f.getFilePointer()-fpos-1);
    }
    //////////////////////////// WRAPPING ///////////////////////////

    ////////////////////////////// GIF //////////////////////////////
    private boolean loadGIF() throws IOException {
        littleendian = true;

        byte[] header = new byte[6];
        f.read(header, 0, 6);
        int mode = 0;
        if (!memcmp(header, "GIF89a", 6)) mode = 2;
        if (!memcmp(header, "GIF87a", 6)) mode = 1;
        if (mode == 0) return false;
        format = "GIF";

        width = ru16();
        height = ru16();
        if (mode < 2) { format = "Old-style GIF"; return true; }

        int flags = ru8();
        f.skipBytes(2);
        if ((flags & 0x80) != 0) f.skipBytes(6<<(flags&0x7));

        while(true) {
            int intro = ru8();
            if (intro == 0x3B) return true;
            else if (intro == 0x2C) {
                f.skipBytes(8);
                flags = ru8();
                if ((flags & 0x80) != 0) f.skipBytes(6<<(flags&0x7));
                f.skipBytes(1);
                int len = ru8();
                if (len < 0 || len > 255) return false;
                while (len > 0) {
                    f.skipBytes(len);
                    len = ru8();
                    if (len < 0 || len > 255) return false;
                }
            } else if (intro == 0x21) {
                long label = ru8();
                if (label == 0xFF) {
                    if (ru8() != 11) return false;
                    byte[] appid = new byte[11];
                    f.read(appid, 0, 11);
                    if (!memcmp(appid, "XMP DataXMP", 11)) {
                        byte[] packet = read_block_delim(f.getFilePointer(), 1);
                        packets.add(packet);
                        f.skipBytes(1); // delimiter, already processed
                        byte[] trailer = new byte[257];
                        f.read(trailer, 0, 257);
                        for(int i=0; i<256; i+=1) if (trailer[i] != (byte)(0xFF - i)) return false;
                        if (trailer[256] != 0) return false;
                    } else {
                        int len = ru8();
                        if (len < 0 || len > 255) return false;
                        while (len > 0) {
                            f.skipBytes(len);
                            len = ru8();
                            if (len < 0 || len > 255) return false;
                        }
                    }
                } else {
                    int len = ru8();
                    if (len < 0 || len > 255) return false;
                    while (len > 0) {
                        f.skipBytes(len);
                        len = ru8();
                        if (len < 0 || len > 255) return false;
                    }
                }
            } else {
                return false;
            }
        }
    }
    ////////////////////////////// GIF //////////////////////////////

    ////////////////////////////// TIFF /////////////////////////////
    private boolean loadTIFF() throws IOException {
        final byte[] length_of_type = {
            -1, // unused
            1, 1, 2, 4, 8, // unsigned byte/ascii/short/int/rational
            1, 1, 2, 4, 8, // signed byte/undef/short/int/rational
            4, 8 // float
        };
        byte[] endflag = new byte[2]; f.read(endflag);
        littleendian = true;
        if (!memcmp(endflag, "MM", 2)) littleendian = false;
        else if (memcmp(endflag, "II", 2)) return false;
        if (ru16() != 42) return false;;

        long offset = ru32();
        while(offset > 0) {
            f.seek(offset);
            int ifd_count = ru16();
            if (ifd_count < 0) return false;
            for(int i=0; i<ifd_count; i+=1) {
                int tag = ru16();
                int type = ru16();
                if (type <= 0 || type > 12) return false;
                long count = ru32();
                long length = count * length_of_type[type];
                long value = ru32();
                if (tag == 256) {
                    if (type == 3 && !littleendian) width = (int)(value>>16)&0xFFFF;
                    else if (type == 3 && littleendian) width = (int)value&0xFFFF;
                    else if (type == 4) width = (int)value;
                    else {
                        System.err.println("Unexpected image width type "+type);
                        return false;
                    }
                } else if (tag == 257) {
                    if (type == 3 && !littleendian) height = (int)(value>>16)&0xFFFF;
                    else if (type == 3 && littleendian) height = (int)value&0xFFFF;
                    else if (type == 4) height = (int)value;
                    else {
                        System.err.println("Unexpected image height type "+type);
                        return false;
                    }
                } else if (tag == 700 && (type == 1 || type == 7) && length > 4) {
                    long back = f.getFilePointer();
                    byte[] xmp = read_block(value, length);
                    if (xmp != null) packets.add(xmp);
                    f.seek(back);
                }
            }
            offset = ru32();
        }
        if (offset == 0) { format = "TIFF"; return true; }
        packets.clear();
        return false;
    }
    ////////////////////////////// TIFF /////////////////////////////


    ///////////////////////////// OTHER /////////////////////////////
    private boolean loadOther() throws IOException {
        String magic = "W5M0MpCehiHzreSzNTczkc9d'?>";
        int midx = 0;
        while(midx < magic.length() && f.getFilePointer() < f.length()) {
            int c = ru8();
            if (c == magic.charAt(midx)) midx += 1;
            else if (c == '"' && magic.charAt(midx) == '\'') midx += 1;
            else midx = 0;
        }
        if (midx == magic.length()) {
            long start = f.getFilePointer();
            magic = "<?xpacket end='w'?>";
            midx = 0;
            while(midx < magic.length() && f.getFilePointer() < f.length()) {
                int c = ru8();
                if (c == magic.charAt(midx)) midx += 1;
                else if (c == '"' && magic.charAt(midx) == '\'') midx += 1;
                else if (c == 'r' && magic.charAt(midx) == 'w') midx += 1;
                else midx = 0;
            }
            long end = f.getFilePointer() - magic.length();
            packets.add(read_block(start, end-start));
            width = -1;
            height = -1;
            format = "unknown image";
            return true;
        }
        return false;
    }
    ///////////////////////////// OTHER /////////////////////////////

    ///////////////////////////// ISOBMF ////////////////////////////
    private class ISOBMFBox {
        public long length;
        public byte[] type;
        public long fpos;
        public ISOBMFBox(long end) throws IOException {
            length = ru32();
            type = new byte[4]; f.read(type, 0, 4);
            if (length == 1) length = ru64() - 8;
            fpos = f.getFilePointer();
            if (length == 0) length = end - fpos;
            else if (length > 0) length -= 8;
        }
        public String toString() {
            return new String(type)+" from "+fpos+" len "+length;
        }
    }
    private boolean loadISOBMF() throws IOException {
        littleendian = false;
        int fmt = 0; format = "ISO/IEC Base Media Format";
        // 0 = unknown, 1 = JPEG2000, 2 = HEIC, 3 = AVIF

        long fsize = f.length();
        ISOBMFBox box = new ISOBMFBox(fsize);
        if (!memcmp(box.type, "jP  ", 4) && box.length == 4) {
            byte[] bit = new byte[4];
            f.read(bit, 0, 4);
            if (bit[0] == '\r' && bit[1] == '\n' && bit[2] == (byte)0x87 && bit[3] == '\n') { fmt = 1; format = "JPEG2000"; }
            else return false;
        } else if (!memcmp(box.type, "ftyp", 4) && box.length >= 12) {
            f.seek(box.fpos + 8);
            byte[] bit = new byte[4];
            for(int i=0; i<(box.length-8)>>2; i+=1) {
                f.read(bit, 0, 4);
                if (!memcmp(bit, "heic", 4)) {
                    fmt = 2;
                    format = "HEIC";
                } else if (!memcmp(bit, "avif", 4)) {
                    fmt = 3;
                    format = "AVIF";
                }
            }
        } else return false; // add other cases if other isobmf supported

        f.seek(box.length + box.fpos);

        while(true) {
            box = new ISOBMFBox(fsize);
            if (box.length < 0) return true;
            if (box.length + box.fpos > fsize) return false;
            if (fmt == 1 && !memcmp(box.type, "jp2h", 4)) {
                while(f.getFilePointer() < box.fpos+box.length) {
                    ISOBMFBox inner = new ISOBMFBox(box.length + box.fpos);
                    if (inner.length < 0) return false;
                    if (inner.length + inner.fpos > box.length + box.fpos) return false;
                    if (!memcmp(inner.type, "ihdr", 4)) {
                        height = (int)ru32();
                        width = (int)ru32();
                    }
                    f.seek(inner.fpos+inner.length);
                }
                f.seek(box.fpos+box.length);
            } else if ((fmt == 2 || fmt == 3) && !memcmp(box.type, "meta", 4)) {
                f.skipBytes(4); // skip 4 bytes, not sure why
                while(f.getFilePointer() < box.fpos+box.length) {
                    ISOBMFBox inner = new ISOBMFBox(box.length + box.fpos);
                    if (inner.length < 0) return false;
                    if (inner.length + inner.fpos > box.length + box.fpos) return false;
                    if (!memcmp(inner.type, "idat", 4)) {
                        f.seek(inner.fpos+4);
                        width = ru16();
                        height = ru16();
                    }
                    else if (!memcmp(inner.type, "iprp", 4)) {
                        while(f.getFilePointer() < inner.fpos+inner.length) {
                            ISOBMFBox in2 = new ISOBMFBox(inner.length + inner.fpos);
                            if (!memcmp(in2.type, "ipco", 4)) {
                                while(f.getFilePointer() < in2.fpos+in2.length) {
                                    ISOBMFBox in3 = new ISOBMFBox(in2.length + in2.fpos);
                                    if (!memcmp(in3.type, "ispe", 4)) {
                                        f.seek(in3.fpos+4);
                                        width = (int)ru32();
                                        height = (int)ru32();
                                    }
                                    f.seek(in3.fpos+in3.length);

                                }
                            }
                            f.seek(in2.fpos+in2.length);
                        }
                    }
                    f.seek(inner.fpos+inner.length);
                }
                f.seek(box.fpos+box.length);
            } else if (!memcmp(box.type, "uuid", 4)) {
                byte[] uuid = new byte[16];
                f.read(uuid, 0, 16);
                if (uuid[0] == (byte)0xBE &&
                    uuid[1] == (byte)0x7A &&
                    uuid[2] == (byte)0xCF &&
                    uuid[3] == (byte)0xCB &&
                    uuid[4] == (byte)0x97 &&
                    uuid[5] == (byte)0xA9 &&
                    uuid[6] == (byte)0x42 &&
                    uuid[7] == (byte)0xE8 &&
                    uuid[8] == (byte)0x9C &&
                    uuid[9] == (byte)0x71 &&
                    uuid[10] == (byte)0x99 &&
                    uuid[11] == (byte)0x94 &&
                    uuid[12] == (byte)0x91 &&
                    uuid[13] == (byte)0xE3 &&
                    uuid[14] == (byte)0xAF &&
                    uuid[15] == (byte)0xAC) {
                    byte[] xmp = read_block(f.getFilePointer(), box.length-16);
                    if (xmp != null) packets.add(xmp);
                }
            }
            f.seek(box.length + box.fpos);
        }
    }
    ///////////////////////////// ISOBMF ////////////////////////////

    ////////////////////////////// JPEG /////////////////////////////
    private boolean loadJPEG() throws IOException {
        littleendian = false;
        byte[] extended = null;

        if (ru8() != 0xFF) return false;
        if (ru8() != 0xD8) return false;
        width = height = 0;

        int m0 = ru8();
        while(f.getFilePointer() < f.length() && m0 >= 0) {
            int m1 = ru8();
            if (m0 == 0xFF && m1 == 0xE1) {
                int len = ru16();
                byte[] buf = new byte[35];
                int got = f.read(buf, 0, 35);
                if (got > 28 && !memcmp(buf, "http://ns.adobe.com/xap/1.0/", 28)) {
                    byte[] packet = read_block(f.getFilePointer() + 29-got, len-31);
                    if (packet != null) packets.add(packet);
                } else if (got > 34 && !memcmp(buf, "http://ns.adobe.com/xmp/extension/", 34)) {
                    // XMP spec says JPEG has two packets, standard and extended; that the extended's GUID is marked; and that the extended follows the standard. But it fails to state that it has *only* two packets, or that all parts of the extended packet must be provided, or that the extended can't be moved earlier.
                    // To avoid needing a GUID:packet mapping, I assume:
                    // 1. standard, then GUID
                    // 2. only one GUID matches standard
                    // 3. all that GUID's parts are present
                    // As I have yet to find an extended XMP in the wild, I haven't been able to test these assumptions
                    if (packets.size() == 0) {
                        System.err.println("WARNING: extended XMP found with no standard XMP; extended ignored");
                        f.skipBytes(len - 2 - got);
                    } else {
                        byte[] guid = new byte[33];
                        f.read(guid, 0, 32);
                        guid[32] = '\0';
                        if (strstr(packets.get(0), guid) >= 0) {
                            long ext_len = ru32();
                            if (extended == null) extended = new byte[(int)ext_len];
                            long ext_off = ru32();
                            f.read(extended, (int)ext_off, len-77);
                        } else {
                            System.err.println("WARNING: extended XMP found with GUID not matching XMP; ignored");
                            f.skipBytes(len - 2 - got);
                        }
                    }
                } else {
                    f.skipBytes(len - 2 - got);
                }
            } else if (m0 == 0xFF && 0xC0 <= m1 && m1 <= 0xCF
                && m1 != 0xC4
                && m1 != 0xCC
            ) {
                f.skipBytes(3);
                // can contain thumbnails, so look for max
                int tmp = ru16();
                if (tmp > height) height = tmp;
                tmp = ru16();
                if (tmp > width) width = tmp;
            } else if (m0 == 0xFF && m1 == 0xDC) {
                f.skipBytes(2);
                int tmp = ru16();
                if (tmp > height) height = tmp;
            }
            m0 = m1;
        }
        if (extended != null) packets.add(extended);
        format = "JPEG";
        return true;
    }
    ////////////////////////////// JPEG /////////////////////////////


    ////////////////////////////// PNG //////////////////////////////
    private static final long[] crc_table = {
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
    private static long feed_crc_buf(long c, byte[] buf) {
        for(byte b : buf)
            c = 0xFFFFFFFFL & (crc_table[(int)(c ^ b) & 0xff] ^ (c >> 8));
        return c;
    }
    private static long feed_crc_buf(long c, byte[] buf, int len) {
        for(int i=0; i<len; i+=1)
            c = 0xFFFFFFFFL & (crc_table[(int)(c ^ buf[i]) & 0xff] ^ (c >> 8));
        return c;
    }
    //private static long feed_crc_buf(long c, String buf) {
        //for(int i=0; i<buf.length(); i+=1)
            //c = 0xFFFFFFFFL & (crc_table[(int)(c ^ buf.charAt(i)) & 0xff] ^ (c >> 8));
        //return c;
    //}
    private static long init_crc() { return 0xffffffffL; }
    private static long feed_crc_u32(long c, long x) {
        c = 0xFFFFFFFFL & (crc_table[(int)(c^((x>>24)&0xFF)) & 0xff] ^ (c>>8));
        c = 0xFFFFFFFFL & (crc_table[(int)(c^((x>>16)&0xFF)) & 0xff] ^ (c>>8));
        c = 0xFFFFFFFFL & (crc_table[(int)(c^((x>>8)&0xFF)) & 0xff] ^ (c>>8));
        c = 0xFFFFFFFFL & (crc_table[(int)(c^(x&0xFF)) & 0xff] ^ (c>>8));
        return c;
    }
    private static long finish_crc(long c) { return c ^ 0xffffffffL; }

    private boolean loadPNG() throws IOException {
        littleendian = false;
        long crc;
        byte[] buf = new byte[22];

        f.read(buf, 0, 8);
        if (buf[0] != (byte)0x89 || buf[1] != 'P' || buf[2] != 'N'
         || buf[3] != 'G' || buf[4] != '\r' || buf[5] != '\n'
         || buf[6] != 0x1A || buf[7] != '\n') return false;

        if (ru32() != 13) return false;
        crc = init_crc();
        f.read(buf, 0, 4); crc=feed_crc_buf(crc, buf, 4);
        if (memcmp(buf, "IHDR", 4)) return false;
        width = (int)ru32(); crc=feed_crc_u32(crc, width);
        height = (int)ru32(); crc=feed_crc_u32(crc, height);
        f.read(buf, 0, 5); crc=feed_crc_buf(crc, buf, 5);
        if ((int)ru32() != finish_crc(crc)) return false;

        while(f.getFilePointer() < f.length()) {
            long length = ru32();
            if (f.getFilePointer() >= f.length() || length < 0) break;
            if (length > 0x7fffffff) return false;
            f.read(buf, 0, 4);
            if (!memcmp(buf, "iTXt", 4) && length > 22) {
                f.read(buf, 0, 22);
                if (!memcmp(buf, "XML:com.adobe.xmp\0\0\0\0\0", 22)) {
                    byte[] xmp = read_block(f.getFilePointer(), length-22);
                    if (xmp != null) packets.add(xmp);
                } else {
                    f.skipBytes((int)length-22);
                }
            } else {
                f.skipBytes((int)length);
            }
            ru32();
        }
        format = "PNG";
        return true;
    }


    ////////////////////////////// WEBP /////////////////////////////
    private boolean loadWEBP() throws IOException {
        littleendian = true;
        byte[] variant = new byte[4];
        byte[] fourcc = new byte[4];
        long fsize = f.length();

        f.read(variant, 0, 4);
        if (memcmp(variant, "RIFF", 4)) return false;
        if (ru32() != fsize-8) return false;
        f.read(variant, 0, 4);
        if (memcmp(variant, "WEBP", 4)) return false;
        f.read(variant, 0, 4);
        long length = ru32();
        format = "WEBP";

        if (!memcmp(variant, "VP8 ", 4)) {
            f.skipBytes(6);
            width = ru16();
            height = ru16();
            return true;
        } else if (!memcmp(variant, "VP8L", 4)) {
            if (ru8() != 0x2F) return false;
            long packed = ru32();
            width = 1 + (int)(packed & 0x3FFF);
            height = 1 + (int)((packed>>14) & 0x3FFF);
            return true;
        } else if (!memcmp(variant, "VP8X", 4)) {
            f.skipBytes(4);
            width = 1 + ru24();
            height = 1 + ru24();
            f.skipBytes((int)length - 10);
            if ((length & 1) != 0) f.skipBytes(1);
        } else return false;

        while(f.getFilePointer() < f.length()) {
            if (f.read(fourcc, 0, 4) != 4) break;
            length = ru32();
            if (!memcmp(fourcc, "XMP ", 4)) {
                byte[] xmp = read_block(f.getFilePointer(), length);
                if (xmp != null) packets.add(xmp);
            } else {
                f.skipBytes((int)length);
            }
            if ((length&1) != 0) f.skipBytes(1);
        }
        return true;
    }
    ////////////////////////////// WEBP /////////////////////////////




    public String toString() {
        if (format == null) return filename+" is not an image";
        StringBuilder tmp = new StringBuilder();
        tmp.append(filename); tmp.append(" is a ");
        if (width >= 0) { tmp.append(width); tmp.append('×'); tmp.append(height);  tmp.append(' '); }
        tmp.append(format); tmp.append(" file with ");
        tmp.append(packets.size());
        tmp.append(packets.size() == 1 ? " XMP packet" : " XMP packets");
        for(byte[] packet : packets) {
            tmp.append("\n ----- packet -----\n");
            tmp.append(new String(packet, UTF8));
            tmp.append("\n ------------------");
        }
        return tmp.toString();
    }
}

