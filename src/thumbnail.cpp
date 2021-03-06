#include "./magickwand.h"

typedef struct thumbnailReq {
  Persistent<Function> cb;
  unsigned char *resizedImage;
  char *exception;
  size_t resizedImageLen;

  unsigned int width;
  unsigned int height;
  unsigned int quality;
  char imagefilepath[1];
} ThumbnailReq;

/* Resize image here */
static void thumbnail(uv_work_t *req) {
  ThumbnailReq *mgr = (ThumbnailReq *)req->data;
  ExceptionType severity;
  MagickWand *magick_wand = NewMagickWand();
  MagickBooleanType status;
  int width, height;
 
  status = MagickReadImage(magick_wand,mgr->imagefilepath);

  if (status == MagickFalse) {
    mgr->exception = MagickGetException(magick_wand,&severity);
    DestroyMagickWand(magick_wand);
    return;
  }

  if (mgr->height == 0 || mgr->width == 0) {
    width = MagickGetImageWidth(magick_wand);
    height = MagickGetImageHeight(magick_wand);

    if (mgr->width == 0 && mgr->height == 0) {
      mgr->width = width;
      mgr->height = height;
    } else {
      float aspectRatio = (width * 1.0)/height;
      if (mgr->height == 0)
        mgr->height =  mgr->width * (1.0/aspectRatio);
      else if (mgr->width == 0)
        mgr->width = mgr->height * aspectRatio;
    }
  }

  MagickThumbnailImage(magick_wand,mgr->width,mgr->height);

  if (mgr->quality) 
    MagickSetImageCompressionQuality(magick_wand,mgr->quality);

  mgr->resizedImage = MagickGetImageBlob(magick_wand,&mgr->resizedImageLen);

  if (!mgr->resizedImage) {
    mgr->exception = MagickGetException(magick_wand,&severity);
  }

  DestroyMagickWand(magick_wand);
}

static void postThumbnail(uv_work_t *req) {
  HandleScope scope;
  ThumbnailReq *mgr = (ThumbnailReq *)req->data;

  Handle<Value> argv[3];
  Local<Object> info = Object::New();

  if (mgr->exception) {
    argv[0] = Exception::Error(String::New(mgr->exception));
    argv[1] = argv[2] = Undefined();
    MagickRelinquishMemory(mgr->exception);
  } else {
    argv[0] = Undefined();
    Buffer *buf = Buffer::New(mgr->resizedImageLen + 1);
    memcpy(Buffer::Data(buf), mgr->resizedImage, mgr->resizedImageLen);
    argv[1] = buf->handle_;
    Local<Integer> width = Integer::New(mgr->width);
    Local<Integer> height = Integer::New(mgr->height);
    Local<Integer> quality = Integer::New(mgr->quality);
    info->Set(String::NewSymbol("width"), width);
    info->Set(String::NewSymbol("height"),height);
    info->Set(String::NewSymbol("quality"),quality);
    argv[2] = info;
  }

  TryCatch try_catch;

  mgr->cb->Call(Context::GetCurrent()->Global(), 3, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  mgr->cb.Dispose();
  MagickRelinquishMemory(mgr->resizedImage);
  free(mgr);

  delete req;
}

Handle<Value> thumbnailAsync (const Arguments& args) {
  HandleScope scope;
  const char *usage = "Too few arguments: Usage: thumbnail(pathtoimgfile,width, height,quality,cb)";
  int argc = 0;
  if (args.Length() != 5) {
    return ThrowException(Exception::Error(String::New(usage)));
  }

  String::Utf8Value name(args[argc++]);
  int width = args[argc++]->Int32Value();
  int height = args[argc++]->Int32Value();
  int quality = args[argc++]->Int32Value();

  Local<Function> cb = Local<Function>::Cast(args[argc++]);

  if (width < 0 || height < 0) {
    return ThrowException(Exception::Error(String::New("Invalid width/height arguments")));
  }
  
  if (quality < 0 || quality > 100) {
    return ThrowException(Exception::Error(String::New("Invalid quality parameter")));
  }

  uv_work_t *req = new uv_work_t;
  ThumbnailReq *mgr = (ThumbnailReq *) calloc(1,sizeof(ThumbnailReq) + name.length());
  req->data = mgr;

  mgr->cb = Persistent<Function>::New(cb);
  mgr->width = width;
  mgr->height = height;
  mgr->quality = quality;

  strncpy(mgr->imagefilepath,*name,name.length() + 1);

  uv_queue_work(uv_default_loop(),req,thumbnail,(uv_after_work_cb)postThumbnail);

  return Undefined();
}
