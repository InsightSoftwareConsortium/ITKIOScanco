/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
/*=========================================================================

  Program: DICOM for VTK

  Copyright (c) 2015 David Gobbi
  All rights reserved.
  See Copyright.txt or http://dgobbi.github.io/bsd3.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "itkScancoImageIO.h"
#include "itkSpatialOrientationAdapter.h"
#include "itkIOCommon.h"
#include "itksys/SystemTools.hxx"
#include "itkMath.h"
#include "itkIntTypes.h"

namespace itk
{

ScancoImageIO
::ScancoImageIO()
{
  this->m_FileType = Binary;
  this->m_ByteOrder = LittleEndian;

  this->AddSupportedWriteExtension(".isq");
  this->AddSupportedWriteExtension(".rsq");
  this->AddSupportedWriteExtension(".rad");
  this->AddSupportedWriteExtension(".aim");

  this->AddSupportedReadExtension(".isq");
  this->AddSupportedReadExtension(".rsq");
  this->AddSupportedReadExtension(".rad");
  this->AddSupportedReadExtension(".aim");

  this->RawHeader = 0;
}


ScancoImageIO
::~ScancoImageIO()
{
  delete [] this->RawHeader;
}


void
ScancoImageIO
::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
}


int
ScancoImageIO
::CheckVersion(const char header[16])
{
  int fileType = 0;

  if (strncmp(header, "CTDATA-HEADER_V1", 16) == 0)
    {
    fileType = 1;
    }
  else if (strcmp(header, "AIMDATA_V030   ") == 0)
    {
    fileType = 3;
    }
  else
    {
    int preHeaderSize = ScancoImageIO::DecodeInt(header);
    int imageHeaderSize = ScancoImageIO::DecodeInt(header + 4);
    if (preHeaderSize == 20 && imageHeaderSize == 140)
      {
      fileType = 2;
      }
    }

  return fileType;
}


int
ScancoImageIO
::DecodeInt(const void *data)
{
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  return (cp[0] | (cp[1] << 8) | (cp[2] << 16) | (cp[3] << 24));
}


float
ScancoImageIO
::DecodeFloat(const void *data)
{
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  // different ordering and exponent bias than IEEE 754 float
  union { float f; unsigned int i; } v;
  v.i = (cp[0] << 16) | (cp[1] << 24) | cp[2] | (cp[3] << 8);
  return 0.25*v.f;
}


double
ScancoImageIO
::DecodeDouble(const void *data)
{
  // different ordering and exponent bias than IEEE 754 double
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  union { double d; uint64_t l; } v;
  unsigned int l1, l2;
  l1 = (cp[0] << 16) | (cp[1] << 24) | cp[2] | (cp[3] << 8);
  l2 = (cp[4] << 16) | (cp[5] << 24) | cp[6] | (cp[7] << 8);
  v.l = (static_cast<uint64_t>(l1) << 32) | l2;
  return v.d*0.25;
}


void
ScancoImageIO
::DecodeDate(const void *data,
  int& year, int& month, int& day,
  int& hour, int& minute, int& second, int& millis)
{
  // This is the offset between the astronomical "Julian day", which counts
  // days since January 1, 4713BC, and the "VMS epoch", which counts from
  // November 17, 1858:
  const uint64_t julianOffset = 2400001;
  const uint64_t millisPerSecond = 1000;
  const uint64_t millisPerMinute = 60 * 1000;
  const uint64_t millisPerHour = 3600 * 1000;
  const uint64_t millisPerDay = 3600 * 24 * 1000;

  // Read the date as a long integer with units of 1e-7 seconds
  int d1 = ScancoImageIO::DecodeInt(data);
  int d2 = ScancoImageIO::DecodeInt(static_cast<const char *>(data)+4);
  uint64_t tVMS = d1 + (static_cast<uint64_t>(d2) << 32);
  uint64_t time = tVMS/10000 + julianOffset*millisPerDay;

  int y, m, d;
  int julianDay = static_cast<int>(time / millisPerDay);
  time -= millisPerDay*julianDay;

  // Gregorian calendar starting from October 15, 1582
  // This algorithm is from Henry F. Fliegel and Thomas C. Van Flandern
  int ell, n, i, j;
  ell = julianDay + 68569;
  n = (4 * ell) / 146097;
  ell = ell - (146097 * n + 3) / 4;
  i = (4000 * (ell + 1)) / 1461001;
  ell = ell - (1461 * i) / 4 + 31;
  j = (80 * ell) / 2447;
  d = ell - (2447 * j) / 80;
  ell = j / 11;
  m = j + 2 - (12 * ell);
  y = 100 * (n - 49) + i + ell;

  // Return the result
  year = y;
  month = m;
  day = d;
  hour = static_cast<int>(time / millisPerHour);
  time -= hour*millisPerHour;
  minute = static_cast<int>(time / millisPerMinute);
  time -= minute*millisPerMinute;
  second = static_cast<int>(time / millisPerSecond);
  time -= second*millisPerSecond;
  millis = static_cast<int>(time);
}


bool
ScancoImageIO
::CanReadFile(const char *filename)
{
  std::ifstream infile(filename, std::ios::in | std::ios::binary);

  bool canRead = false;
  if (infile.good())
    {
    // header is a 512 byte block
    char buffer[512];
    infile.read(buffer, 512);
    if (!infile.bad())
      {
      int fileType = ScancoImageIO::CheckVersion(buffer);
      canRead = (fileType > 0);
      }
    }

  infile.close();

  return canRead;
}


void
ScancoImageIO
::InitializeHeader()
{
  memset(this->Version, 0, 18);
  memset(this->PatientName, 0, 42);
  memset(this->CreationDate, 0, 32);
  memset(this->ModificationDate, 0, 32);
  this->ScanDimensionsPixels[0] = 0;
  this->ScanDimensionsPixels[1] = 0;
  this->ScanDimensionsPixels[2] = 0;
  this->ScanDimensionsPhysical[0] = 0;
  this->ScanDimensionsPhysical[1] = 0;
  this->ScanDimensionsPhysical[2] = 0;
  this->PatientIndex = 0;
  this->ScannerID = 0;
  this->SliceThickness = 0;
  this->SliceIncrement = 0;
  this->StartPosition = 0;
  this->EndPosition = 0;
  this->ZPosition = 0;
  this->DataRange[0] = 0;
  this->DataRange[1] = 0;
  this->MuScaling = 1.0;
  this->NumberOfSamples = 0;
  this->NumberOfProjections = 0;
  this->ScanDistance = 0;
  this->SampleTime = 0;
  this->ScannerType = 0;
  this->MeasurementIndex = 0;
  this->Site = 0;
  this->ReconstructionAlg = 0;
  this->ReferenceLine = 0;
  this->Energy = 0;
  this->Intensity = 0;

  this->RescaleType = 0;
  memset(this->RescaleUnits, 0, 18);
  memset(this->CalibrationData, 0, 66);
  this->RescaleSlope = 1.0;
  this->RescaleIntercept = 0.0;
  this->MuWater = 0;

  this->Compression = 0;
}


void
ScancoImageIO
::StripString(char *dest, const char *cp, size_t l)
{
  char *dp = dest;
  for (size_t i = 0; i < l && *cp != '\0'; ++i)
    {
    *dp++ = *cp++;
    }
  while (dp != dest && dp[-1] == ' ')
    {
    --dp;
    }
  *dp = '\0';
}


int
ScancoImageIO
::ReadISQHeader(std::ifstream *file, unsigned long bytesRead)
{
  if (bytesRead < 512)
    {
    return 0;
    }

  char *h = this->RawHeader;
  ScancoImageIO::StripString(this->Version, h, 16); h += 16;
  int dataType = ScancoImageIO::DecodeInt(h); h += 4;
  /*int numBytes = ScancoImageIO::DecodeInt(h);*/ h += 4;
  /*int numBlocks = ScancoImageIO::DecodeInt(h);*/ h += 4;
  this->PatientIndex = ScancoImageIO::DecodeInt(h); h += 4;
  this->ScannerID = ScancoImageIO::DecodeInt(h); h += 4;
  int year, month, day, hour, minute, second, milli;
  ScancoImageIO::DecodeDate(
    h, year, month, day, hour, minute, second, milli); h += 8;
  int pixdim[3], physdim[3];
  pixdim[0] = ScancoImageIO::DecodeInt(h); h += 4;
  pixdim[1] = ScancoImageIO::DecodeInt(h); h += 4;
  pixdim[2] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[0] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[1] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[2] = ScancoImageIO::DecodeInt(h); h += 4;

  const bool isRAD = (dataType == 9 || physdim[2] == 0);

  if (isRAD) // RAD file
    {
    this->MeasurementIndex = ScancoImageIO::DecodeInt(h); h += 4;
    this->DataRange[0] = ScancoImageIO::DecodeInt(h); h += 4;
    this->DataRange[1] = ScancoImageIO::DecodeInt(h); h += 4;
    this->MuScaling = ScancoImageIO::DecodeInt(h); h += 4;
    ScancoImageIO::StripString(this->PatientName, h, 40); h += 40;
    this->ZPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    /* unknown */ h += 4;
    this->SampleTime = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->Energy = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->Intensity = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->ReferenceLine = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->StartPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->EndPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    h += 88*4;
    }
  else // ISQ file or RSQ file
    {
    this->SliceThickness = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->SliceIncrement = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->StartPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->EndPosition =
      this->StartPosition + physdim[2]*1e-3*(pixdim[2] - 1)/pixdim[2];
    this->DataRange[0] = ScancoImageIO::DecodeInt(h); h += 4;
    this->DataRange[1] = ScancoImageIO::DecodeInt(h); h += 4;
    this->MuScaling = ScancoImageIO::DecodeInt(h); h += 4;
    this->NumberOfSamples = ScancoImageIO::DecodeInt(h); h += 4;
    this->NumberOfProjections = ScancoImageIO::DecodeInt(h); h += 4;
    this->ScanDistance = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->ScannerType = ScancoImageIO::DecodeInt(h); h += 4;
    this->SampleTime = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->MeasurementIndex = ScancoImageIO::DecodeInt(h); h += 4;
    this->Site = ScancoImageIO::DecodeInt(h); h += 4;
    this->ReferenceLine = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->ReconstructionAlg = ScancoImageIO::DecodeInt(h); h += 4;
    ScancoImageIO::StripString(this->PatientName, h, 40); h += 40;
    this->Energy = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->Intensity = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    h += 83*4;
    }

  int dataOffset = ScancoImageIO::DecodeInt(h);

  // fix SliceThickness and SliceIncrement if they were truncated
  if (physdim[2] != 0)
    {
    double computedSpacing = physdim[2]*1e-3/pixdim[2];
    if (fabs(computedSpacing - this->SliceThickness) < 1.1e-3)
      {
      this->SliceThickness = computedSpacing;
      }
    if (fabs(computedSpacing - this->SliceIncrement) < 1.1e-3)
      {
      this->SliceIncrement = computedSpacing;
      }
    }

  // Convert date information into a string
  month = ((month > 12 || month < 1) ? 0 : month);
  static const char *months[] = { "XXX", "JAN", "FEB", "MAR", "APR", "MAY",
    "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
  sprintf(this->CreationDate, "%d-%s-%d %02d:%02d:%02d.%03d",
          (day % 100), months[month], (year % 10000),
          (hour % 100), (minute % 100), (second % 100), (milli % 1000));
  sprintf(this->ModificationDate, "%d-%s-%d %02d:%02d:%02d.%03d",
          (day % 100), months[month], (year % 10000),
          (hour % 100), (minute % 100), (second % 100), (milli % 1000));

  // Perform a sanity check on the dimensions
  for (int i = 0; i < 3; ++i)
    {
    this->ScanDimensionsPixels[i] = pixdim[i];
    if (pixdim[i] < 1)
      {
      pixdim[i] = 1;
      }
    this->ScanDimensionsPhysical[i] =
      (isRAD ? physdim[i]*1e-6 : physdim[i]*1e-3);
    if (physdim[i] == 0)
      {
      physdim[i] = 1.0;
      }
    }

  this->SetNumberOfDimensions( 3 );
  for ( unsigned int i = 0; i < m_NumberOfDimensions; ++i )
    {
    this->SetDimensions(i, pixdim[i] - 1);
    if (isRAD) // RAD file
      {
      if( i == 2 )
        {
        this->SetSpacing( i, 1.0 );
        }
      else
        {
        this->SetSpacing( i, physdim[i]*1e-6/pixdim[i] );
        }
      }
    else
      {
      this->SetSpacing( i, physdim[i]*1e-3/pixdim[i] );
      }
    this->SetOrigin( i, 0.0 );
    }

  this->SetPixelType(SCALAR);
  this->SetComponentType(SHORT);

  // total header size
  unsigned long headerSize = static_cast<unsigned long>(dataOffset + 1)*512;
  //this->SetHeaderSize(headerSize);

  // read the rest of the header
  if (headerSize > bytesRead)
    {
    h = new char[headerSize];
    memcpy(h, this->RawHeader, bytesRead);
    delete [] this->RawHeader;
    this->RawHeader = h;
    file->read(h + bytesRead, headerSize - bytesRead);
    if (static_cast<unsigned long>(file->gcount()) < headerSize - bytesRead)
      {
      return 0;
      }
    }

  // decode the extended header (lots of guesswork)
  if (headerSize >= 2048)
    {
    char *calHeader = 0;
    int calHeaderSize = 0;
    h = this->RawHeader + 512;
    unsigned long hskip = 1;
    char *headerName = h + 8;
    if (strncmp(headerName, "MultiHeader     ", 16) == 0)
      {
      h += 512;
      hskip += 1;
      }
    unsigned long hsize = 0;
    for (int i = 0; i < 4; ++i)
      {
      hsize = ScancoImageIO::DecodeInt(h + i*128 + 24);
      if ((1 + hskip + hsize)*512 > headerSize)
        {
        break;
        }
      headerName = h + i*128 + 8;
      if (strncmp(headerName, "Calibration     ", 16) == 0)
        {
        calHeader = this->RawHeader + (1 + hskip)*512;
        calHeaderSize = hsize*512;
        }
      hskip += hsize;
      }

    if (calHeader && calHeaderSize >= 1024)
      {
      h = calHeader;
      ScancoImageIO::StripString(this->CalibrationData, h + 28, 64);
      // std::string calFile(h + 112, 256);
      // std::string s3(h + 376, 256);
      this->RescaleType = ScancoImageIO::DecodeInt(h + 632);
      ScancoImageIO::StripString(this->RescaleUnits, h + 648, 16);
      // std::string s5(h + 700, 16);
      // std::string calFilter(h + 772, 16);
      this->RescaleSlope = ScancoImageIO::DecodeDouble(h + 664);
      this->RescaleIntercept = ScancoImageIO::DecodeDouble(h + 672);
      this->MuWater = ScancoImageIO::DecodeDouble(h + 688);
      }
    }

  // Include conversion to linear att coeff in the rescaling
  if (this->MuScaling != 0)
    {
    this->RescaleSlope /= this->MuScaling;
    }

  return 1;
}


int
ScancoImageIO
::ReadAIMHeader(std::ifstream *file, unsigned long bytesRead)
{
  if (bytesRead < 160)
    {
    return 0;
    }

  char *h = this->RawHeader;
  int intSize = 0;
  unsigned long headerSize = 0;
  if (strcmp(h, "AIMDATA_V030   ") == 0)
    {
    // header uses 64-bit ints (8 bytes)
    intSize = 8;
    strcpy(this->Version, h);
    headerSize = 16;
    h += headerSize;
    }
  else
    {
    // header uses 32-bit ints (4 bytes)
    intSize = 4;
    strcpy(this->Version, "AIMDATA_V020   ");
    }

  // read the pre-header
  char *preheader = h;
  int preheaderSize = ScancoImageIO::DecodeInt(h); h += intSize;
  int structSize = ScancoImageIO::DecodeInt(h); h += intSize;
  int logSize = ScancoImageIO::DecodeInt(h); h += intSize;

  // read the rest of the header
  headerSize += preheaderSize + structSize + logSize;
  //this->SetHeaderSize(headerSize);
  if (headerSize > bytesRead)
    {
    h = new char[headerSize];
    memcpy(h, this->RawHeader, bytesRead);
    preheader = h + (preheader - this->RawHeader);
    delete [] this->RawHeader;
    this->RawHeader = h;
    file->read(h + bytesRead, headerSize - bytesRead);
    if (static_cast<unsigned long>(file->gcount()) < headerSize - bytesRead)
      {
      return 0;
      }
    }

  // decode the struct header
  h = preheader + preheaderSize;
  h += 20; // not sure what these 20 bytes are for
  int dataType = ScancoImageIO::DecodeInt(h); h += 4;
  int structValues[21];
  for (int i = 0; i < 21; ++i)
    {
    structValues[i] = ScancoImageIO::DecodeInt(h); h += intSize;
    }
  float elementSize[3];
  for (int i = 0; i < 3; ++i)
    {
    elementSize[i] = ScancoImageIO::DecodeFloat(h);
    if (elementSize[i] == 0)
      {
      elementSize[i] = 1.0;
      }
    h += 4;
    }

  // number of components per pixel is 1 by default
  this->SetPixelType( SCALAR );
  this->Compression = 0;

  // a limited selection of data types are supported
  // (only 0x00010001 (char) and 0x00020002 (short) are fully tested)
  switch (dataType)
  {
    case 0x00160001:
      this->SetComponentType( UCHAR );
      break;
    case 0x000d0001:
      this->SetComponentType( UCHAR );
      break;
    case 0x00120003:
      this->SetComponentType( UCHAR );
      this->SetPixelType( VECTOR );
      this->SetNumberOfDimensions( 3 );
      break;
    case 0x00010001:
      this->SetComponentType( CHAR );
      break;
    case 0x00060003:
      this->SetComponentType( CHAR );
      this->SetPixelType( VECTOR );
      this->SetNumberOfDimensions( 3 );
      break;
    case 0x00170002:
      this->SetComponentType( USHORT );
      break;
    case 0x00020002:
      this->SetComponentType( SHORT );
      break;
    case 0x00030004:
      this->SetComponentType( INT );
      break;
    case 0x001a0004:
      this->SetComponentType( FLOAT );
      break;
    case 0x00150001:
      this->Compression = 0x00b2; // run-length compressed bits
      this->SetComponentType( CHAR );
      break;
    case 0x00080002:
      this->Compression = 0x00c2; // run-length compressed signed char
      this->SetComponentType( CHAR );
      break;
    case 0x00060001:
      this->Compression = 0x00b1; // packed bits
      this->SetComponentType( CHAR );
      break;
    default:
      itkExceptionMacro("Unrecognized data type in AIM file: " << dataType);
  }

  this->SetNumberOfDimensions( 3 );
  for ( unsigned int i = 0; i < m_NumberOfDimensions; ++i )
    {
    this->SetDimensions(i, structValues[3 + i] - 1);
    this->SetSpacing( i, elementSize[i] );
    // the origin will reflect the cropping of the data
    this->SetOrigin( i, elementSize[i] * structValues[i] );
    }

  // decode the processing log
  h = preheader + preheaderSize + structSize;
  char *logEnd = h + logSize;

  while (h != logEnd && *h != '\0')
    {
    // skip newline and go to next line
    if (*h == '\n')
      {
      ++h;
      }

    // search for the end of this line
    char *lineEnd = h;
    while (lineEnd != logEnd && *lineEnd != '\n' && *lineEnd != '\0')
      {
      ++lineEnd;
      }

    // if not a comment, search for keys
    if (h != lineEnd && *h != '!' && (*lineEnd == '\n' || *lineEnd == '\0'))
      {
      // key and value are separated by multiple spaces
      char *key = h;
      while (h+1 != lineEnd && (h[0] != ' ' || h[1] != ' '))
        {
        ++h;
        }
      // this gives the length of the key
      size_t keylen = h - key;
      // skip to the end of the spaces
      while (h != lineEnd && *h == ' ')
        {
        ++h;
        }
      // this is where the value starts
      char *value = h;
      size_t valuelen = lineEnd - value;
      // look for trailing spaces
      while (valuelen > 0 && (h[valuelen-1] == ' ' || h[valuelen-1] == '\r'))
        {
        --valuelen;
        }

      // convert into a std::string for convenience
      std::string skey(key, keylen);

      // check for known keys
      if (skey == "Time")
        {
        valuelen = (valuelen > 31 ? 31 : valuelen);
        strncpy(this->ModificationDate, value, valuelen);
        this->ModificationDate[valuelen] = '\0';
        }
      else if (skey == "Original Creation-Date")
        {
        valuelen = (valuelen > 31 ? 31 : valuelen);
        strncpy(this->CreationDate, value, valuelen);
        this->CreationDate[valuelen] = '\0';
        }
      else if (skey == "Orig-ISQ-Dim-p")
        {
        for (int i = 0; i < 3; i++)
          {
          this->ScanDimensionsPixels[i] = strtol(value, &value, 10);
          }
        }
      else if (skey == "Orig-ISQ-Dim-um")
        {
        for (int i = 0; i < 3; i++)
          {
          this->ScanDimensionsPhysical[i] = strtod(value, &value)*1e-3;
          }
        }
      else if (skey == "Patient Name")
        {
        valuelen = (valuelen > 41 ? 41 : valuelen);
        strncpy(this->PatientName, value, valuelen);
        this->PatientName[valuelen] = '\0';
        }
      else if (skey == "Index Patient")
        {
        this->PatientIndex = strtol(value, 0, 10);
        }
      else if (skey == "Index Measurement")
        {
        this->MeasurementIndex = strtol(value, 0, 10);
        }
      else if (skey == "Site")
        {
        this->Site = strtol(value, 0, 10);
        }
      else if (skey == "Scanner ID")
        {
        this->ScannerID = strtol(value, 0, 10);
        }
      else if (skey == "Scanner type")
        {
        this->ScannerType = strtol(value, 0, 10);
        }
      else if (skey == "Position Slice 1 [um]")
        {
        this->StartPosition = strtod(value, 0)*1e-3;
        this->EndPosition =
          this->StartPosition + elementSize[2]*(structValues[5] - 1);
        }
      else if (skey == "No. samples")
        {
        this->NumberOfSamples = strtol(value, 0, 10);
        }
      else if (skey == "No. projections per 180")
        {
        this->NumberOfProjections = strtol(value, 0, 10);
        }
      else if (skey == "Scan Distance [um]")
        {
        this->ScanDistance = strtod(value, 0)*1e-3;
        }
      else if (skey == "Integration time [us]")
        {
        this->SampleTime = strtod(value, 0)*1e-3;
        }
      else if (skey == "Reference line [um]")
        {
        this->ReferenceLine = strtod(value, 0)*1e-3;
        }
      else if (skey == "Reconstruction-Alg.")
        {
        this->ReconstructionAlg = strtol(value, 0, 10);
        }
      else if (skey == "Energy [V]")
        {
        this->Energy = strtod(value, 0)*1e-3;
        }
      else if (skey == "Intensity [uA]")
        {
        this->Intensity = strtod(value, 0)*1e-3;
        }
      else if (skey == "Mu_Scaling")
        {
        this->MuScaling = strtol(value, 0, 10);
        }
      else if (skey == "Minimum data value")
        {
        this->DataRange[0] = strtod(value, 0);
        }
      else if (skey == "Maximum data value")
        {
        this->DataRange[1] = strtod(value, 0);
        }
      else if (skey == "Calib. default unit type")
        {
        this->RescaleType = strtol(value, 0, 10);
        }
      else if (skey == "Calibration Data")
        {
        valuelen = (valuelen > 64 ? 64 : valuelen);
        strncpy(this->CalibrationData, value, valuelen);
        this->CalibrationData[valuelen] = '\0';
        }
      else if (skey == "Density: unit")
        {
        valuelen = (valuelen > 16 ? 16 : valuelen);
        strncpy(this->RescaleUnits, value, valuelen);
        this->RescaleUnits[valuelen] = '\0';
        }
      else if (skey == "Density: slope")
        {
        this->RescaleSlope = strtod(value, 0);
        }
      else if (skey == "Density: intercept")
        {
        this->RescaleIntercept = strtod(value, 0);
        }
      else if (skey == "HU: mu water")
        {
        this->MuWater = strtod(value, 0);
        }
      }
    // skip to the end of the line
    h = lineEnd;
    }

  // Include conversion to linear att coeff in the rescaling
  if (this->MuScaling != 0)
    {
    this->RescaleSlope /= this->MuScaling;
    }

  // these items are not in the processing log
  this->SliceThickness = elementSize[2];
  this->SliceIncrement = elementSize[2];

  return 1;
}


void
ScancoImageIO
::ReadImageInformation()
{
  this->InitializeHeader();

  if( this->m_FileName.empty() )
    {
    itkExceptionMacro( "FileName has not been set." );
    }


  std::ifstream infile( m_FileName.c_str(), std::ios::in | std::ios::binary);
  if ( !infile.good() )
    {
    itkExceptionMacro( "Cannot open file: " << m_FileName );
    }

  // header is a 512 byte block
  this->RawHeader = new char[512];
  infile.read(this->RawHeader, 512);
  int fileType = 0;
  unsigned long bytesRead = 0;
  if (!infile.bad())
    {
    bytesRead = static_cast<unsigned long>(infile.gcount());
    fileType = ScancoImageIO::CheckVersion(this->RawHeader);
    }

  if (fileType == 0)
    {
    infile.close();
    itkExceptionMacro( "Unrecognized header in: " << m_FileName );
    }

  if (fileType == 1)
    {
    this->ReadISQHeader(&infile, bytesRead);
    }
  else
    {
    this->ReadAIMHeader(&infile, bytesRead);
    }

  infile.close();

  // This code causes rescaling to Hounsfield units
  /*
  if (this->MuScaling > 0 && this->MuWater > 0)
    {
    // HU = 1000*(u - u_water)/u_water
    this->RescaleSlope = 1000.0/(this->MuWater * this->MuScaling);
    this->RescaleIntercept = -1000.0;
    }
  */
}

void ScancoImageIO::Read(void *buffer)
{
  const unsigned int nDims = this->GetNumberOfDimensions();

  // this will check to see if we are actually streaming
  // we initialize with the dimensions of the file, since if
  // largestRegion and ioRegion don't match, we'll use the streaming
  // path since the comparison will fail
  ImageIORegion largestRegion(nDims);

  for ( unsigned int i = 0; i < nDims; i++ )
    {
    largestRegion.SetIndex(i, 0);
    largestRegion.SetSize( i, this->GetDimensions(i) );
    }

  if ( largestRegion != m_IORegion )
    {
    int *indexMin = new int[nDims];
    int *indexMax = new int[nDims];
    for ( unsigned int i = 0; i < nDims; i++ )
      {
      if ( i < m_IORegion.GetImageDimension() )
        {
        indexMin[i] = m_IORegion.GetIndex()[i];
        indexMax[i] = indexMin[i] + m_IORegion.GetSize()[i] - 1;
        }
      else
        {
        indexMin[i] = 0;
        // this is zero since this is a (size - 1)
        indexMax[i] = 0;
        }
      }

    if ( !m_ScancoImage.ReadROI(indexMin, indexMax,
                              m_FileName.c_str(), true, buffer,
                              m_SubSamplingFactor) )
      {
      delete[] indexMin;
      delete[] indexMax;
      itkExceptionMacro( "File cannot be read: "
                         << this->GetFileName() << " for reading."
                         << std::endl
                         << "Reason: "
                         << itksys::SystemTools::GetLastSystemError() );
      }

    delete[] indexMin;
    delete[] indexMax;

    m_ScancoImage.ElementByteOrderFix( m_IORegion.GetNumberOfPixels() );
    }
  else
    {
    if ( !m_ScancoImage.Read(m_FileName.c_str(), true, buffer) )
      {
      itkExceptionMacro( "File cannot be read: "
                         << this->GetFileName() << " for reading."
                         << std::endl
                         << "Reason: "
                         << itksys::SystemTools::GetLastSystemError() );
      }

    // since we are not streaming m_IORegion may not be set, so
    m_ScancoImage.ElementByteOrderFix( this->GetImageSizeInPixels() );
    }
}

ScancoImage * ScancoImageIO::GetScancoImagePointer(void)
{
  return &m_ScancoImage;
}

bool ScancoImageIO::CanWriteFile(const char *name)
{
  std::string filename = name;

  if (  filename == "" )
    {
    return false;
    }

  std::string::size_type mhaPos = filename.rfind(".mha");
  if ( ( mhaPos != std::string::npos )
       && ( mhaPos == filename.length() - 4 ) )
    {
    return true;
    }

  std::string::size_type mhdPos = filename.rfind(".mhd");
  if ( ( mhdPos != std::string::npos )
       && ( mhdPos == filename.length() - 4 ) )
    {
    return true;
    }

  return false;
}

void
ScancoImageIO
::WriteImageInformation(void)
{
  ScancoDataDictionary & metaDict = this->GetScancoDataDictionary();
  std::string          metaDataStr;

  // Look at default metaio fields
  if ( ExposeScancoData< std::string >(metaDict, ITK_VoxelUnits, metaDataStr) )
    {
    // Handle analyze style unit string
    if ( metaDataStr == "um. " )
      {
      m_ScancoImage.DistanceUnits(MET_DISTANCE_UNITS_UM);
      }
    else if ( metaDataStr == "mm. " )
      {
      m_ScancoImage.DistanceUnits(MET_DISTANCE_UNITS_MM);
      }
    else if ( metaDataStr == "cm. " )
      {
      m_ScancoImage.DistanceUnits(MET_DISTANCE_UNITS_CM);
      }
    else
      {
      m_ScancoImage.DistanceUnits( metaDataStr.c_str() );
      }
    }

  if ( ExposeScancoData< std::string >(metaDict, ITK_ExperimentDate, metaDataStr) )
    {
    m_ScancoImage.AcquisitionDate( metaDataStr.c_str() );
    }

  // Save out the metadatadictionary key/value pairs as part of
  // the metaio header.
  std::vector< std::string > keys = metaDict.GetKeys();
  std::vector< std::string >::const_iterator keyIt;
  for ( keyIt = keys.begin(); keyIt != keys.end(); ++keyIt )
    {
    if(*keyIt == ITK_ExperimentDate ||
       *keyIt == ITK_VoxelUnits)
      {
      continue;
      }
    // try for common scalar types
    std::ostringstream strs;
    double dval=0.0;
    float fval=0.0F;
    long lval=0L;
    unsigned long ulval=0L;
    int ival=0;
    unsigned uval=0;
    short shval=0;
    unsigned short ushval=0;
    char cval=0;
    unsigned char ucval=0;
    bool bval=false;
    std::string value="";
    if(ExposeScancoData< std::string >(metaDict, *keyIt, value))
      {
      strs << value;
      }
    else if(ExposeScancoData<double>(metaDict,*keyIt,dval))
      {
      strs << dval;
      }
    else if(ExposeScancoData<float>(metaDict,*keyIt,fval))
      {
      strs << fval;
      }
    else if(ExposeScancoData<long>(metaDict,*keyIt,lval))
      {
      strs << lval;
      }
    else if(ExposeScancoData<unsigned long>(metaDict,*keyIt,ulval))
      {
      strs << ulval;
      }
    else if(ExposeScancoData<int>(metaDict,*keyIt,ival))
      {
      strs << ival;
      }
    else if(ExposeScancoData<unsigned int>(metaDict,*keyIt,uval))
      {
      strs << uval;
      }
    else if(ExposeScancoData<short>(metaDict,*keyIt,shval))
      {
      strs << shval;
      }
    else if(ExposeScancoData<unsigned short>(metaDict,*keyIt,ushval))
      {
      strs << ushval;
      }
    else if(ExposeScancoData<char>(metaDict,*keyIt,cval))
      {
      strs << cval;
      }
    else if(ExposeScancoData<unsigned char>(metaDict,*keyIt,ucval))
      {
      strs << ucval;
      }
    else if(ExposeScancoData<bool>(metaDict,*keyIt,bval))
      {
      strs << bval;
      }

    value = strs.str();

    if (value == "" )
      {
      // if the value is an empty string then the resulting entry in
      // the header will not be able to be read the the metaIO
      // library, which results is a unreadable/corrupt file.
      itkWarningMacro("Unsupported or empty metaData item "
                      << *keyIt << " of type "
                      << metaDict[*keyIt]->GetScancoDataObjectTypeName()
                      << "found, won't be written to image file");

      // so this entry should be skipped.
      continue;
      }

    // Rolling this back out so that the tests pass.
    // The meta image AddUserField requires control of the memory space.
    m_ScancoImage.AddUserField( (*keyIt).c_str(), MET_STRING, static_cast<int>( value.size() ), value.c_str(), true, -1 );
    }

}

/**
 *
 */
void
ScancoImageIO
::Write(const void *buffer)
{
  const unsigned int numberOfDimensions = this->GetNumberOfDimensions();

  bool binaryData = true;

  if ( this->GetFileType() == ASCII )
    {
    binaryData = false;
    }

  int nChannels = this->GetNumberOfComponents();

  MET_ValueEnumType eType = MET_OTHER;
  switch ( m_ComponentType )
    {
    default:
    case UNKNOWNCOMPONENTTYPE:
      eType = MET_OTHER;
      break;
    case CHAR:
      eType = MET_CHAR;
      break;
    case UCHAR:
      eType = MET_UCHAR;
      break;
    case SHORT:
      eType = MET_SHORT;
      break;
    case USHORT:
      eType = MET_USHORT;
      break;
    case LONG:
      if ( sizeof( long ) == MET_ValueTypeSize[MET_LONG] )
        {
        eType = MET_LONG;
        }
      else if ( sizeof( long ) == MET_ValueTypeSize[MET_INT] )
        {
        eType = MET_INT;
        }
      else if ( sizeof( long ) == MET_ValueTypeSize[MET_LONG_LONG] )
        {
        eType = MET_LONG_LONG;
        }
      break;
    case ULONG:
      if ( sizeof( long ) == MET_ValueTypeSize[MET_LONG] )
        {
        eType = MET_ULONG;
        }
      else if ( sizeof( long ) == MET_ValueTypeSize[MET_INT] )
        {
        eType = MET_UINT;
        }
      else if ( sizeof( long ) == MET_ValueTypeSize[MET_LONG_LONG] )
        {
        eType = MET_ULONG_LONG;
        }
      break;
    case INT:
      eType = MET_INT;
      if ( sizeof( int ) == MET_ValueTypeSize[MET_INT] )
        {
        eType = MET_INT;
        }
      else if ( sizeof( int ) == MET_ValueTypeSize[MET_LONG] )
        {
        eType = MET_LONG;
        }
      break;
    case UINT:
      if ( sizeof( int ) == MET_ValueTypeSize[MET_INT] )
        {
        eType = MET_UINT;
        }
      else if ( sizeof( int ) == MET_ValueTypeSize[MET_LONG] )
        {
        eType = MET_ULONG;
        }
      break;
    case FLOAT:
      if ( sizeof( float ) == MET_ValueTypeSize[MET_FLOAT] )
        {
        eType = MET_FLOAT;
        }
      else if ( sizeof( float ) == MET_ValueTypeSize[MET_DOUBLE] )
        {
        eType = MET_DOUBLE;
        }
      break;
    case DOUBLE:
      if ( sizeof( double ) == MET_ValueTypeSize[MET_DOUBLE] )
        {
        eType = MET_DOUBLE;
        }
      else if ( sizeof( double ) == MET_ValueTypeSize[MET_FLOAT] )
        {
        eType = MET_FLOAT;
        }
      break;
    }

  int *        dSize = new int[numberOfDimensions];
  float *      eSpacing = new float[numberOfDimensions];
  double *     eOrigin = new double[numberOfDimensions];
  for ( unsigned int ii = 0; ii < numberOfDimensions; ++ii )
    {
    dSize[ii] = this->GetDimensions(ii);
    eSpacing[ii] = static_cast< float >( this->GetSpacing(ii) );
    eOrigin[ii] = this->GetOrigin(ii);
    }

  m_ScancoImage.InitializeEssential( numberOfDimensions, dSize, eSpacing, eType, nChannels,
                                   const_cast< void * >( buffer ) );
  m_ScancoImage.Position(eOrigin);
  m_ScancoImage.BinaryData(binaryData);

  //Write the image Information
  this->WriteImageInformation();

  if ( numberOfDimensions == 3 )
    {
    SpatialOrientation::ValidCoordinateOrientationFlags coordOrient =
      SpatialOrientation::ITK_COORDINATE_ORIENTATION_INVALID;
    std::vector< double > dirx, diry, dirz;
    SpatialOrientationAdapter::DirectionType dir;
    dirx = this->GetDirection(0);
    diry = this->GetDirection(1);
    dirz = this->GetDirection(2);
    for ( unsigned ii = 0; ii < 3; ii++ )
      {
      dir[ii][0] = dirx[ii];
      dir[ii][1] = diry[ii];
      dir[ii][2] = dirz[ii];
      }
    coordOrient = SpatialOrientationAdapter().FromDirectionCosines(dir);

    switch ( coordOrient )
      {
      default:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSP:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_RL);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSP:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_LR);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASR:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_AP);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSR:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_PA);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRP:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_IS);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRP:
        {
        m_ScancoImage.AnatomicalOrientation(0, MET_ORIENTATION_SI);
        break;
        }
      }
    switch ( coordOrient )
      {
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRP:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_RL);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLP:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_LR);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAR:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_AP);
        break;
        }
      default:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPR:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_PA);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIP:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_IS);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSP:
        {
        m_ScancoImage.AnatomicalOrientation(1, MET_ORIENTATION_SI);
        break;
        }
      }
    switch ( coordOrient )
      {
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAR:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPR:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_RL);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PSL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_AIL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ASL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IPL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SAL:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SPL:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_LR);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLA:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRA:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_AP);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LIP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LSP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RIP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RSP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ILP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_IRP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SLP:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_SRP:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_PA);
        break;
        }
      default:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAI:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPI:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_IS);
        break;
        }
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PLS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_PRS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ALS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_ARS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_LPS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RAS:
      case SpatialOrientation::ITK_COORDINATE_ORIENTATION_RPS:
        {
        m_ScancoImage.AnatomicalOrientation(2, MET_ORIENTATION_SI);
        break;
        }
      }
    }
  // Propagage direction cosine information.
  double *transformMatrix = static_cast< double * >( malloc( numberOfDimensions * numberOfDimensions * sizeof( double ) ) );
  if (transformMatrix)
    {
    for ( unsigned int ii = 0; ii < numberOfDimensions; ++ii )
      {
      for ( unsigned int jj = 0; jj < numberOfDimensions; ++jj )
        {
        transformMatrix[ii * numberOfDimensions + jj] =
          this->GetDirection(ii)[jj];
        }
      }
    m_ScancoImage.TransformMatrix(transformMatrix);
    free(transformMatrix);
    }

  m_ScancoImage.CompressedData(m_UseCompression);

  // this is a check to see if we are actually streaming
  // we initialize with m_IORegion to match dimensions
  ImageIORegion largestRegion(m_IORegion);
  for ( unsigned int ii = 0; ii < numberOfDimensions; ++ii )
    {
    largestRegion.SetIndex( ii, 0 );
    largestRegion.SetSize( ii, this->GetDimensions(ii) );
    }

  if ( m_UseCompression && ( largestRegion != m_IORegion ) )
    {
    std::cout << "Compression in use: cannot stream the file writing" << std::endl;
    }
  else if (  largestRegion != m_IORegion )
    {
    int *indexMin = new int[numberOfDimensions];
    int *indexMax = new int[numberOfDimensions];
    for ( unsigned int ii = 0; ii < numberOfDimensions; ++ii )
      {
      // the dimensions of m_IORegion should match out requested
      // dimensions, but ImageIORegion will throw an
      // exception if out of bounds
      indexMin[ii] = m_IORegion.GetIndex()[ii];
      indexMax[ii] = m_IORegion.GetIndex()[ii] + m_IORegion.GetSize()[ii] - 1;
      }

    if ( !m_ScancoImage.WriteROI( indexMin, indexMax, m_FileName.c_str() ) )
      {
      delete[] dSize;
      delete[] eSpacing;
      delete[] eOrigin;
      delete[] indexMin;
      delete[] indexMax;
      itkExceptionMacro( "File ROI cannot be written: "
                         << this->GetFileName()
                         << std::endl
                         << "Reason: "
                         << itksys::SystemTools::GetLastSystemError() );
      }

    delete[] indexMin;
    delete[] indexMax;
    }
  else
    {
    if ( !m_ScancoImage.Write( m_FileName.c_str() ) )
      {
      delete[] dSize;
      delete[] eSpacing;
      delete[] eOrigin;
      itkExceptionMacro( "File cannot be written: "
                         << this->GetFileName()
                         << std::endl
                         << "Reason: "
                         << itksys::SystemTools::GetLastSystemError() );
      }
    }

  delete[] dSize;
  delete[] eSpacing;
  delete[] eOrigin;
}

/** Given a requested region, determine what could be the region that we can
 * read from the file. This is called the streamable region, which will be
 * smaller than the LargestPossibleRegion and greater or equal to the
 * RequestedRegion */
ImageIORegion
ScancoImageIO
::GenerateStreamableReadRegionFromRequestedRegion(const ImageIORegion & requestedRegion) const
{
  //
  // The default implementations determines that the streamable region is
  // equal to the largest possible region of the image.
  //
  ImageIORegion streamableRegion(this->m_NumberOfDimensions);

  if ( !m_UseStreamedReading )
    {
    for ( unsigned int i = 0; i < this->m_NumberOfDimensions; i++ )
      {
      streamableRegion.SetSize(i, this->m_Dimensions[i]);
      streamableRegion.SetIndex(i, 0);
      }
    }
  else
    {
    streamableRegion = requestedRegion;
    }

  return streamableRegion;
}

unsigned int
ScancoImageIO::GetActualNumberOfSplitsForWriting(unsigned int numberOfRequestedSplits,
                                               const ImageIORegion & pasteRegion,
                                               const ImageIORegion & largestPossibleRegion)
{
  if ( this->GetUseCompression() )
    {
    // we can not stream or paste with compression
    if ( pasteRegion != largestPossibleRegion )
      {
      itkExceptionMacro( "Pasting and compression is not supported! Can't write:" << this->GetFileName() );
      }
    else if ( numberOfRequestedSplits != 1 )
      {
      itkDebugMacro("Requested streaming and compression");
      itkDebugMacro("Scanco IO is not streaming now!");
      }
    return 1;
    }

  if ( !itksys::SystemTools::FileExists( m_FileName.c_str() ) )
    {
    // file doesn't exits so we don't have potential problems
    }
  else if ( pasteRegion != largestPossibleRegion )
    {
    // we are going to be pasting (may be streaming too)

    // need to check to see if the file is compatible
    std::string errorMessage;
    Pointer     headerImageIOReader = Self::New();

    try
      {
      headerImageIOReader->SetFileName( m_FileName.c_str() );
      headerImageIOReader->ReadImageInformation();
      }
    catch ( ... )
      {
      errorMessage = "Unable to read information from file: " + m_FileName;
      }

    // we now need to check that the following match:
    // 1)file is not compressed
    // 2)pixel type
    // 3)dimensions
    // 4)size/origin/spacing
    // 5)direction cosines
    //

    if ( errorMessage.size() )
      {
      // 0) Can't read file
      }
    // 1)file is not compressed
    else if ( headerImageIOReader->m_ScancoImage.CompressedData() )
      {
      errorMessage = "File is compressed: " + m_FileName;
      }
    // 2)pixel type
    // this->GetPixelType() is not verified because the metaio file format
    // stores all multi-component types as arrays, so it does not
    // distinguish between pixel types. Also as long as the compoent
    // and number of compoents match we should be able to paste, that
    // is the numbers should be the same it is just the interpretation
    // that is not matching
    else if ( headerImageIOReader->GetNumberOfComponents() != this->GetNumberOfComponents()
              || headerImageIOReader->GetComponentType() != this->GetComponentType() )
      {
      errorMessage = "Component type does not match in file: " + m_FileName;
      }
    // 3)dimensions/size
    else if ( headerImageIOReader->GetNumberOfDimensions() != this->GetNumberOfDimensions() )
      {
      errorMessage = "Dimensions does not match in file: " + m_FileName;
      }
    else
      {
      for ( unsigned int i = 0; i < this->GetNumberOfDimensions(); ++i )
        {
        // 4)size/origin/spacing
        if ( headerImageIOReader->GetDimensions(i) != this->GetDimensions(i)
             || Math::NotExactlyEquals(headerImageIOReader->GetSpacing(i), this->GetSpacing(i))
             || Math::NotExactlyEquals(headerImageIOReader->GetOrigin(i), this->GetOrigin(i)) )
          {
          errorMessage = "Size, spacing or origin does not match in file: " + m_FileName;
          break;
          }
        // 5)direction cosines
        if ( headerImageIOReader->GetDirection(i) != this->GetDirection(i) )
          {
          errorMessage = "Direction cosines does not match in file: " + m_FileName;
          break;
          }
        }
      }

    if ( errorMessage.size() )
      {
      itkExceptionMacro("Unable to paste because pasting file exists and is different. " << errorMessage);
      }
    else if ( headerImageIOReader->GetPixelType() != this->GetPixelType() )
      {
      // since there is currently poor support for pixel types in
      // ScancoIO we will just warn when it does not match
      itkWarningMacro("Pixel types does not match file, but component type and number of components do.");
      }
    }
  else if ( numberOfRequestedSplits != 1 )
    {
    // we are going be streaming

    // need to remove the file incase the file doesn't match our
    // current header/meta data information
    if ( !itksys::SystemTools::RemoveFile( m_FileName.c_str() ) )
      {
      itkExceptionMacro("Unable to remove file for streaming: " << m_FileName);
      }
    }

  return GetActualNumberOfSplitsForWritingCanStreamWrite(numberOfRequestedSplits, pasteRegion);
}

ImageIORegion
ScancoImageIO::GetSplitRegionForWriting( unsigned int ithPiece,
                                       unsigned int numberOfActualSplits,
                                       const ImageIORegion & pasteRegion,
                                       const ImageIORegion & itkNotUsed(largestPossibleRegion) )
{
  return GetSplitRegionForWritingCanStreamWrite(ithPiece, numberOfActualSplits, pasteRegion);
}
} // end namespace itk
