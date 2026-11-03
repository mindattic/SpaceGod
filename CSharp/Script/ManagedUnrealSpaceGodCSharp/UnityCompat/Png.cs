using System;
using System.IO;
using System.IO.Compression;

namespace UnityEngine
{
    /// <summary>A minimal RGBA8 PNG writer for runtime textures (UnityBridge.TextureFromPixels). Unity's pixel rows run
    /// bottom-up, a PNG's top-down, so rows are written in reverse.</summary>
    internal static class Png
    {
        public static byte[] Encode(int width, int height, Color32[] pixels)
        {
            var raw = new byte[height * (width * 4 + 1)];
            int o = 0;
            for (int y = height - 1; y >= 0; y--)
            {
                raw[o++] = 0; // filter: none
                for (int x = 0; x < width; x++)
                {
                    var p = pixels[y * width + x];
                    raw[o++] = p.r; raw[o++] = p.g; raw[o++] = p.b; raw[o++] = p.a;
                }
            }
            using var ms = new MemoryStream();
            ms.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A });
            var ihdr = new byte[13];
            BigEndian(ihdr, 0, (uint)width);
            BigEndian(ihdr, 4, (uint)height);
            ihdr[8] = 8; ihdr[9] = 6; // 8-bit RGBA
            Chunk(ms, "IHDR", ihdr);
            using (var z = new MemoryStream())
            {
                using (var zl = new ZLibStream(z, CompressionLevel.Fastest, true)) zl.Write(raw);
                Chunk(ms, "IDAT", z.ToArray());
            }
            Chunk(ms, "IEND", Array.Empty<byte>());
            return ms.ToArray();
        }

        static void BigEndian(byte[] b, int at, uint v)
        {
            b[at] = (byte)(v >> 24); b[at + 1] = (byte)(v >> 16); b[at + 2] = (byte)(v >> 8); b[at + 3] = (byte)v;
        }

        static readonly uint[] Crc = MakeCrc();
        static uint[] MakeCrc()
        {
            var t = new uint[256];
            for (uint n = 0; n < 256; n++)
            {
                uint c = n;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[n] = c;
            }
            return t;
        }

        static void Chunk(Stream s, string type, byte[] data)
        {
            var len = new byte[4];
            BigEndian(len, 0, (uint)data.Length);
            s.Write(len);
            var head = System.Text.Encoding.ASCII.GetBytes(type);
            s.Write(head);
            s.Write(data);
            uint c = 0xFFFFFFFFu;
            foreach (var b in head) c = Crc[(c ^ b) & 0xFF] ^ (c >> 8);
            foreach (var b in data) c = Crc[(c ^ b) & 0xFF] ^ (c >> 8);
            var crc = new byte[4];
            BigEndian(crc, 0, ~c);
            s.Write(crc);
        }
    }
}
