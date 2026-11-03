using System;
using UnrealSharp.Engine;

namespace UnityEngine.Networking
{
    /// <summary>
    /// Unity's UnityWebRequest as SkyView uses it: a texture from a local file (a dropped-in skybox, a file:// URI). Unreal
    /// decodes it with RenderingLibrary.ImportFileAsTexture2D when the request is sent; the request is done at once.
    /// </summary>
    public class UnityWebRequest : IDisposable
    {
        public enum Result { InProgress, Success, ConnectionError, ProtocolError, DataProcessingError }

        internal readonly string Uri;
        internal Texture2D? texture;
        public bool isDone { get; private set; }
        public Result result { get; private set; } = Result.InProgress;
        public string? error { get; private set; }

        internal UnityWebRequest(string uri) { Uri = uri; }

        public AsyncOperation SendWebRequest()
        {
            var op = new AsyncOperation();
            try
            {
                string path = new System.Uri(Uri).LocalPath;
                var t = UnityBridge.HasHost ? RenderingLibrary.ImportFileAsTexture2D(path) : null;
                if (t != null)
                {
                    texture = Texture2D.FromUnreal(t, System.IO.Path.GetFileNameWithoutExtension(path));
                    result = Result.Success;
                }
                else
                {
                    result = Result.DataProcessingError;
                    error = "could not decode " + path;
                }
            }
            catch (Exception e)
            {
                result = Result.ConnectionError;
                error = e.Message;
            }
            isDone = true;
            op.Finish();
            return op;
        }

        public void Abort() { }
        public void Dispose() { }
    }

    public static class UnityWebRequestTexture
    {
        public static UnityWebRequest GetTexture(string uri, bool nonReadable = false) => new UnityWebRequest(uri);
    }

    public static class DownloadHandlerTexture
    {
        public static Texture2D GetContent(UnityWebRequest request) => request.texture ?? new Texture2D(1, 1);
    }
}
