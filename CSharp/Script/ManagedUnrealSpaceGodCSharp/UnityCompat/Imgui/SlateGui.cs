using System;
using System.Runtime.InteropServices;
using UnrealSharp.Binds;

namespace UnityEngine
{
    /// <summary>
    /// US-T54: the game module's Slate GUI overlay (CSharp/Source/UnrealSpaceGodCSharp/SpaceGodGuiOverlay.cpp), reached
    /// through UnrealSharp's binds registry (the "SpaceGodGui" binder). The Repaint pass is recorded there -- clips,
    /// texture quads, text runs -- and a Slate widget over the game viewport paints it with clipping zones, as the C++
    /// track's GuiHost does, so text at a clip edge is cut through its glyphs. Also Slate's font measure and the texture
    /// upload with Unity's mip chain. <see cref="Available"/> is false when the binder is missing (the HUD then falls
    /// back to <see cref="HudCanvas"/>).
    /// </summary>
    internal static unsafe class SlateGui
    {
        const string Binder = "SpaceGodGui";

        static delegate* unmanaged[Cdecl]<void> begin, end;
        static delegate* unmanaged[Cdecl]<float, float, float, float, int, void> clip;
        static delegate* unmanaged[Cdecl]<IntPtr, float, float, float, float, float, float, float, float, float, float, float, float, float, void> texture;
        static delegate* unmanaged[Cdecl]<char*, int, float, float, float, float, int, int, float, float, float, float, void> text;
        static delegate* unmanaged[Cdecl]<char*, int, int, int, float> measure;
        static delegate* unmanaged[Cdecl]<int, int, float> lineHeight;
        static delegate* unmanaged[Cdecl]<byte*, int, int, int, int, int, int, IntPtr> create;

        static bool? available;

        /// <summary>Whether the overlay's functions were found (looked up once).</summary>
        public static bool Available => available ??= Bind();

        static bool Bind()
        {
            try
            {
                // The sizes are the native signatures' (UnrealSharp checks them): pointers 8, int and float 4, the return included.
                begin = (delegate* unmanaged[Cdecl]<void>)NativeBinds.TryGetBoundFunction(Binder, "Begin", 0);
                end = (delegate* unmanaged[Cdecl]<void>)NativeBinds.TryGetBoundFunction(Binder, "End", 0);
                clip = (delegate* unmanaged[Cdecl]<float, float, float, float, int, void>)NativeBinds.TryGetBoundFunction(Binder, "Clip", 20);
                texture = (delegate* unmanaged[Cdecl]<IntPtr, float, float, float, float, float, float, float, float, float, float, float, float, float, void>)
                    NativeBinds.TryGetBoundFunction(Binder, "Texture", 8 + 13 * 4);
                text = (delegate* unmanaged[Cdecl]<char*, int, float, float, float, float, int, int, float, float, float, float, void>)
                    NativeBinds.TryGetBoundFunction(Binder, "Text", 8 + 4 + 4 * 4 + 2 * 4 + 4 * 4);
                measure = (delegate* unmanaged[Cdecl]<char*, int, int, int, float>)NativeBinds.TryGetBoundFunction(Binder, "Measure", 4 + 8 + 3 * 4);
                lineHeight = (delegate* unmanaged[Cdecl]<int, int, float>)NativeBinds.TryGetBoundFunction(Binder, "LineHeight", 4 + 2 * 4);
                create = (delegate* unmanaged[Cdecl]<byte*, int, int, int, int, int, int, IntPtr>)NativeBinds.TryGetBoundFunction(Binder, "CreateTexture", 8 + 8 + 6 * 4);
                return true;
            }
            catch (Exception e)
            {
                Debug.LogWarning("[Unity.SpaceGod] Slate GUI overlay not bound (" + e.Message + "); the HUD's Canvas draws the GUI");
                return false;
            }
        }

        public static void Begin() => begin();
        public static void End() => end();
        public static void Clip(Rect? r)
        {
            if (r.HasValue) clip(r.Value.x, r.Value.y, r.Value.width, r.Value.height, 1);
            else clip(0, 0, 0, 0, 0);
        }

        public static void Texture(IntPtr native, Rect screen, Rect uv, Color linear, float rotation) =>
            texture(native, screen.x, screen.y, screen.width, screen.height, uv.x, uv.y, uv.xMax, uv.yMax, linear.r, linear.g, linear.b, linear.a, rotation);

        public static void Text(string s, Vector2 at, float width, float height, int size, FontStyle style, Color linear)
        {
            fixed (char* p = s) text(p, s.Length, at.x, at.y, width, height, size, (int)style, linear.r, linear.g, linear.b, linear.a);
        }

        public static float Measure(string s, int size, FontStyle style)
        {
            fixed (char* p = s) return measure(p, s.Length, size, (int)style);
        }

        public static float LineHeight(int size, FontStyle style) => lineHeight(size, (int)style);

        /// <summary>A new Unreal texture made from pixels with Unity's mip chain, filter, wrap and colour space
        /// (<see cref="Texture2D.Apply()"/>), or null. US-T66: made and filled natively before its first render resource,
        /// not by rewriting an imported texture's mips while the import's resource still initialises.</summary>
        public static UnrealSharp.Engine.UTexture2D? Create(Color32[] pixels, int width, int height, bool mipChain, FilterMode filter, TextureWrapMode wrap, bool linear)
        {
            if (width <= 0 || height <= 0 || pixels.Length < width * height) return null;
            var bytes = MemoryMarshal.AsBytes(pixels.AsSpan());
            int w = wrap == TextureWrapMode.Clamp ? 1 : wrap == TextureWrapMode.Mirror || wrap == TextureWrapMode.MirrorOnce ? 2 : 0;
            IntPtr native;
            fixed (byte* p = bytes) native = create(p, width, height, mipChain ? 1 : 0, (int)filter, w, linear ? 1 : 0);
            if (native == IntPtr.Zero) return null;
            // The UObject* to its managed wrapper, as UnrealSharp marshals an object return.
            return UnrealSharp.Core.Marshallers.ObjectMarshaller<UnrealSharp.Engine.UTexture2D>.FromNative((IntPtr)(&native), 0);
        }
    }
}
