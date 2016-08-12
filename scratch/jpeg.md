JPEG Compression/Decompression
==============================


StackOverflow answer:
http://stackoverflow.com/questions/9094691/examples-or-tutorials-of-using-libjpeg-turbos-turbojpeg


Compression
-----------

```
#include <turbojpeg.h>

const int JPEG_QUALITY = 75;
const int COLOR_COMPONENTS = 3;
int _width = 1920;
int _height = 1080;
long unsigned int _jpegSize = 0;
unsigned char* _compressedImage = NULL; //!< Memory is allocated by tjCompress2 if _jpegSize == 0
unsigned char buffer[_width*_height*COLOR_COMPONENTS]; //!< Contains the uncompressed image

tjhandle _jpegCompressor = tjInitCompress();

tjCompress2(_jpegCompressor, buffer, _width, 0, _height, TJPF_RGB,
          &_compressedImage, &_jpegSize, TJSAMP_444, JPEG_QUALITY,
          TJFLAG_FASTDCT);

tjDestroy(_jpegCompressor);

//to free the memory allocated by TurboJPEG (either by tjAlloc(), 
//or by the Compress/Decompress) after you are done working on it:
tjFree(&_compressedImage);
```

After that you have the compressed image in _compressedImage. 
To decompress you have to do the following:

Decompression
-------------

```
#include <turbojpeg.h>

long unsigned int _jpegSize; //!< _jpegSize from above
unsigned char* _compressedImage; //!< _compressedImage from above

int jpegSubsamp, width, height;
unsigned char buffer[width*height*COLOR_COMPONENTS]; //!< will contain the decompressed image

tjhandle _jpegDecompressor = tjInitDecompress();

tjDecompressHeader2(_jpegDecompressor, _compressedImage, _jpegSize, &width, &height, &jpegSubsamp);

tjDecompress2(_jpegDecompressor, _compressedImage, _jpegSize, buffer, width, 0/*pitch*/, height, TJPF_RGB, TJFLAG_FASTDCT);

tjDestroy(_jpegDecompressor);
```

Some random thoughts:
---------------------

I just came back over this as I am writing my bachelor thesis, 
and I noticed that if you run the compression in a loop it is preferable 
to store the biggest size of the JPEG buffer to not have to allocate a 
new one every turn. Basically, instead of doing:

```
long unsigned int _jpegSize = 0;

tjCompress2(_jpegCompressor, buffer, _width, 0, _height, TJPF_RGB,
          &_compressedImage, &_jpegSize, TJSAMP_444, JPEG_QUALITY,
          TJFLAG_FASTDCT);
```          

we would add an object variable, holding the size of the allocated memory
long unsigned int _jpegBufferSize = 0; and before every compression 
round we would set the jpegSize back to that value:

```
long unsigned int jpegSize = _jpegBufferSize;

tjCompress2(_jpegCompressor, buffer, _width, 0, _height, TJPF_RGB,
          &_compressedImage, &jpegSize, TJSAMP_444, JPEG_QUALITY,
          TJFLAG_FASTDCT);

_jpegBufferSize = _jpegBufferSize >= jpegSize? _jpegBufferSize : jpegSize;
```