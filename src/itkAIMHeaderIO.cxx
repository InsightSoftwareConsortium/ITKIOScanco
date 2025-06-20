/*=========================================================================
 *
 *  Copyright NumFOCUS
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#include "itkAIMHeaderIO.h"
#include <sstream>
#include <cstring>

constexpr const char * AIM020String = "AIMDATA_V020   ";
constexpr const char * AIM030String = "AIMDATA_V030   ";

typedef char            EncodedByte;
typedef char            EncodedInt4Byte[4];
typedef char            EncodedInt8Byte[8];
typedef EncodedInt4Byte EncodedTuple4Byte[3];
typedef EncodedInt8Byte EncodedTuple8Byte[3];


struct AIMV020AssociatedData
{
  EncodedInt4Byte m_ID;
  EncodedInt4Byte m_Data;
  EncodedInt4Byte m_NR;
  EncodedInt4Byte m_Size;
  EncodedInt4Byte m_Type;
};

struct AIMV030AssociatedData
{
  EncodedInt4Byte m_ID;
  EncodedInt4Byte m_NR;
  EncodedInt4Byte m_Size;
  EncodedInt4Byte m_Type;
};

struct AIMV020StructHeader
{
  EncodedInt4Byte       m_Version;
  EncodedInt4Byte       m_ProcLog;
  EncodedInt4Byte       m_Data;
  EncodedInt4Byte       m_ID;
  EncodedInt4Byte       m_Reference;
  EncodedInt4Byte       m_Type;
  EncodedTuple4Byte     m_Position;
  EncodedTuple4Byte     m_Dimension;
  EncodedTuple4Byte     m_Offset;
  EncodedTuple4Byte     m_SupDimension;
  EncodedTuple4Byte     m_SupPosition;
  EncodedTuple4Byte     m_SubDimension;
  EncodedTuple4Byte     m_TestOffset;
  EncodedTuple4Byte     m_ElementSize;
  AIMV020AssociatedData m_AssociatedData;
};

struct AIMV030StructHeader
{
  EncodedInt4Byte       m_Version;
  EncodedInt4Byte       m_ID;
  EncodedInt4Byte       m_Reference;
  EncodedInt4Byte       m_Type;
  EncodedTuple8Byte     m_Position;
  EncodedTuple8Byte     m_Dimension;
  EncodedTuple8Byte     m_Offset;
  EncodedTuple8Byte     m_SupDimension;
  EncodedTuple8Byte     m_SupPosition;
  EncodedTuple8Byte     m_SubDimension;
  EncodedTuple8Byte     m_TestOffset;
  EncodedTuple8Byte     m_ElementSize;
  AIMV030AssociatedData m_AssociatedData;
};

struct AIMPreHeaderV020
{
  EncodedInt4Byte m_PreHeaderLength;
  EncodedInt4Byte m_ImageStructLength;
  EncodedInt4Byte m_ProcessingLogLength;
  EncodedInt4Byte m_ImageDataLength;
  EncodedInt4Byte m_AssociatedDataLength;
};

struct AIMPreHeaderV030
{
  EncodedInt8Byte m_PreHeaderLength;
  EncodedInt8Byte m_ImageStructLength;
  EncodedInt8Byte m_ProcessingLogLength;
  EncodedInt8Byte m_ImageDataLength;
  EncodedInt8Byte m_AssociatedDataLength;
};

namespace itk
{
unsigned long
AIMHeaderIO::ReadHeader(std::ifstream & infile)
{
  unsigned long bytesRead = 0; // Use as offset for reading the header

  if (!infile.is_open())
  {
    throw std::runtime_error("AIMHeaderIO: Input file stream is not open.");
  }

  // Populate the data structure with the raw header information
  // Initially read one block to retrieve the header size and version
  char * headerBytes = new char[ScancoHeaderBlockSize];
  infile.read(headerBytes, ScancoHeaderBlockSize);
  int versionType = CheckVersion(headerBytes);

  // The size of the data values depends on the file version
  switch (versionType)
  {
    case static_cast<int>(ScancoFileVersions::AIM_020):
      this->m_IntSize = 4; // AIM v020 uses 32-bit [4 byte] integers
      strcpy(this->m_HeaderData->m_Version, AIM020String);
      break;

    case static_cast<int>(ScancoFileVersions::AIM_030):
      this->m_IntSize = 8; // AIM v030 uses 64-bit [8 byte] integers
      strcpy(this->m_HeaderData->m_Version, AIM030String);
      bytesRead += 16; // Skip the version string
      break;

    default:
      throw std::runtime_error("AIMHeaderIO: Unrecognized file version.");
      break;
  }

  // Read the pre-header to get remaining header and log size
  this->m_PreHeaderSize = DecodeInt(headerBytes + bytesRead);

  this->ReadPreHeader(infile, this->m_PreHeaderSize, bytesRead);
  bytesRead += this->m_PreHeaderSize;

  unsigned long headerSize = this->m_PreHeaderSize + this->m_ImgStructSize + this->m_ProcessingLogSize;

  if (headerSize > ScancoHeaderBlockSize)
  {
    // Allocate more space for the header and read the rest into the raw header
    delete[] headerBytes;
    delete[] this->m_HeaderData->m_RawHeader;
    headerBytes = new char[headerSize];
    // headerSize does not include the version string from v030
    infile.seekg(0, std::ios::beg);
    infile.read(headerBytes, headerSize);
  }
  // we have read the full header, save to our data structure
  this->m_HeaderData->m_RawHeader = headerBytes;

  // Read the image structure header (version-dependent)
  switch (versionType)
  {
    case static_cast<int>(ScancoFileVersions::AIM_020):
    {
      AIMV020StructHeader * structHeader = new AIMV020StructHeader;
      memcpy((char *)structHeader, headerBytes + bytesRead, this->m_ImgStructSize);
      this->ReadImgStructHeader(structHeader);
      delete structHeader;
      break;
    }
    case static_cast<int>(ScancoFileVersions::AIM_030):
    {
      AIMV030StructHeader * structHeader = new AIMV030StructHeader;
      memcpy((char *)structHeader, headerBytes + bytesRead, this->m_ImgStructSize);
      this->ReadImgStructHeader(structHeader);
      delete structHeader;
      break;
    }
    default:
      throw std::runtime_error("Error: invalid AIM type.");
      break;
  }
  bytesRead += this->m_ImgStructSize;

  // Read the processing log
  this->ReadProcessingLog(infile, bytesRead, this->m_ProcessingLogSize);
  bytesRead += this->m_ProcessingLogSize;

  // these items are not in the processing log
  this->m_HeaderData->m_SliceThickness = this->m_HeaderData->m_PixelData.m_Spacing[2];
  this->m_HeaderData->m_SliceIncrement = this->m_HeaderData->m_PixelData.m_Spacing[2];

  return headerSize; // Return the size of the header
}

unsigned long
AIMHeaderIO::WriteHeader(std::ofstream & outfile, unsigned long imageSize)
{
  throw std::runtime_error("ScancoHeaderIO::ReadHeader(std::ifstream&) not implemented.");
}

int
AIMHeaderIO::ReadPreHeader(std::ifstream & file, unsigned long length, unsigned long offset)
{
  unsigned long bytesRead = this->m_IntSize; // We assume the pre-header length (int) has already been read

  if (!file.is_open())
  {
    return -1;
  }

  if (length == 0)
  {
    return -1; // No pre-header to read
  }

  // Seek to the offset to skip the version string if necessary
  file.seekg(offset, std::ios::beg);

  char * preHeader = new char[length];
  file.read(preHeader, length);

  this->m_ImgStructSize = DecodeInt(preHeader + bytesRead);
  bytesRead += this->m_IntSize;
  this->m_ProcessingLogSize = DecodeInt(preHeader + bytesRead);
  bytesRead += this->m_IntSize;

  // these items are not in the processing log
  this->m_HeaderData->m_SliceThickness = this->m_HeaderData->m_PixelData.m_Spacing[2];
  this->m_HeaderData->m_SliceIncrement = this->m_HeaderData->m_PixelData.m_Spacing[2];

  delete[] preHeader;
  return 0; // Success
}

void
AIMHeaderIO::ReadImgStructHeader(AIMV020StructHeader * headerData)
{

  this->m_HeaderData->m_PixelData.m_ComponentType = DecodeInt(headerData->m_Type);

  int i = 0;
  for (int & imageDimension : this->m_HeaderData->m_PixelData.m_Dimensions)
  {
    imageDimension = DecodeInt(headerData->m_Dimension[i]);
    i++;
  }

  // Element spacing is stored as a float
  i = 0;
  for (double & spacing : this->m_HeaderData->m_PixelData.m_Spacing)
  {
    spacing = DecodeFloat(headerData->m_ElementSize[i]);
    if (spacing == 0)
    {
      spacing = 1.0;
    }
    i++;
  }

  // Set the pixel data origin
  for (int i = 0; i < 3; ++i)
  {
    this->m_HeaderData->m_PixelData.m_Origin[i] =
      DecodeInt(headerData->m_Position[i]) * this->m_HeaderData->m_PixelData.m_Spacing[i];
  }
}

void
AIMHeaderIO::ReadImgStructHeader(AIMV030StructHeader * headerData)
{
  this->m_HeaderData->m_PixelData.m_ComponentType = DecodeInt(headerData->m_Type);

  int i = 0;
  for (int & imageDimension : this->m_HeaderData->m_PixelData.m_Dimensions)
  {
    imageDimension = DecodeInt(headerData->m_Dimension[i]);
    i++;
  }

  // element spacing is a 64-bit integer
  i = 0;
  for (double & spacing : this->m_HeaderData->m_PixelData.m_Spacing)
  {
    spacing = 1e-6 * DecodeInt(headerData->m_ElementSize[i]);
    if (spacing == 0)
    {
      spacing = 1.0;
    }
    i++;
  }

  // Set the pixel data origin
  for (int i = 0; i < 3; ++i)
  {
    this->m_HeaderData->m_PixelData.m_Origin[i] =
      DecodeInt(headerData->m_Position[i]) * this->m_HeaderData->m_PixelData.m_Spacing[i];
  }
}

int
AIMHeaderIO::ReadProcessingLog(std::ifstream & infile, unsigned long offset, unsigned long length)
{
  int         bytesRead = 0;
  std::string readString = "";

  if (length == 0)
  {
    return -1; // No image structure header to read
  }

  infile.seekg(offset, std::ios::beg);

  while (bytesRead < length)
  {
    getline(infile, readString);

    if (infile.eof() || infile.fail() || infile.bad())
    {
      return -1; // Error reading the file
    }

    bytesRead += readString.length() + 1; // +1 for the newline character

    if (readString[0] == '!')
    {
      // Skip comment lines
      continue;
    }

    // Assume keys are separated by (at least) two spaces
    std::string key = readString.substr(0, readString.find("  "));

    std::string value = readString.substr(readString.find("  ") + 2);

    if (value.find_first_not_of(" ") == std::string::npos)
    {
      continue;
    }

    value = value.substr(value.find_first_not_of(" "),
                         value.find_last_not_of(' ') - value.find_first_not_of(" ") + 1); // Trim trailing spaces

    // check for known keys
    if (key == "Time")
    {
      strncpy(this->m_HeaderData->m_ModificationDate, value.c_str(), value.length() + 1);
    }
    else if (key == "Original Creation-Date")
    {
      strncpy(this->m_HeaderData->m_CreationDate, value.c_str(), value.length() + 1);
    }
    else if (key == "Orig-ISQ-Dim-p")
    {
      size_t processed = 0;
      for (int & ScanDimensionsPixel : this->m_HeaderData->m_ScanDimensionsPixels)
      {
        ScanDimensionsPixel = std::stol(value, &processed);
        value = value.substr(processed);
      }
    }
    else if (key == "Orig-ISQ-Dim-um")
    {
      size_t processed = 0;
      for (double & i : this->m_HeaderData->m_ScanDimensionsPhysical)
      {
        i = stod(value, &processed) * 1e-3;
        value = value.substr(processed);
      }
    }
    else if (key == "Patient Name")
    {
      strncpy(this->m_HeaderData->m_PatientName, value.c_str(), value.length() + 1);
    }
    else if (key == "Index Patient")
    {
      this->m_HeaderData->m_PatientIndex = stol(value);
    }
    else if (key == "Index Measurement")
    {
      this->m_HeaderData->m_MeasurementIndex = stol(value);
    }
    else if (key == "Site")
    {
      this->m_HeaderData->m_Site = stol(value);
    }
    else if (key == "Scanner ID")
    {
      this->m_HeaderData->m_ScannerID = stol(value);
    }
    else if (key == "Scanner type")
    {
      this->m_HeaderData->m_ScannerType = stol(value);
    }
    else if (key == "Position Slice 1 [um]")
    {
      this->m_HeaderData->m_StartPosition = stod(value) * 1e-3;
      this->m_HeaderData->m_EndPosition =
        this->m_HeaderData->m_StartPosition +
        this->m_HeaderData->m_PixelData.m_Spacing[2] * (this->m_HeaderData->m_PixelData.m_Dimensions[2] - 1);
    }
    else if (key == "No. samples")
    {
      this->m_HeaderData->m_NumberOfSamples = stol(value);
    }
    else if (key == "No. projections per 180")
    {
      this->m_HeaderData->m_NumberOfProjections = stol(value);
    }
    else if (key == "Scan Distance [um]")
    {
      this->m_HeaderData->m_ScanDistance = stod(value) * 1e-3;
    }
    else if (key == "Integration time [us]")
    {
      this->m_HeaderData->m_SampleTime = stod(value) * 1e-3;
    }
    else if (key == "Reference line [um]")
    {
      this->m_HeaderData->m_ReferenceLine = stod(value) * 1e-3;
    }
    else if (key == "Reconstruction-Alg.")
    {
      this->m_HeaderData->m_ReconstructionAlg = stol(value);
    }
    else if (key == "Energy [V]")
    {
      this->m_HeaderData->m_Energy = stod(value) * 1e-3;
    }
    else if (key == "Intensity [uA]")
    {
      this->m_HeaderData->m_Intensity = stod(value) * 1e-3;
    }
    else if (key == "Mu_Scaling")
    {
      this->m_HeaderData->m_MuScaling = stol(value);
    }
    else if (key == "Minimum data value")
    {
      this->m_HeaderData->m_DataRange[0] = stod(value);
    }
    else if (key == "Maximum data value")
    {
      this->m_HeaderData->m_DataRange[1] = stod(value);
    }
    else if (key == "Calib. default unit type")
    {
      this->m_HeaderData->m_RescaleType = stol(value);
    }
    else if (key == "Calibration Data")
    {
      strncpy(this->m_HeaderData->m_CalibrationData, value.c_str(), value.length() + 1);
    }
    else if (key == "Density: unit")
    {
      strncpy(this->m_HeaderData->m_RescaleUnits, value.c_str(), value.length() + 1);
    }
    else if (key == "Density: slope")
    {
      this->m_HeaderData->m_RescaleSlope = stod(value);
    }
    else if (key == "Density: intercept")
    {
      this->m_HeaderData->m_RescaleIntercept = stod(value);
    }
    else if (key == "HU: mu water")
    {
      this->m_HeaderData->m_MuWater = stod(value);
    }
  }
  return bytesRead;
}

AIMV020StructHeader
AIMHeaderIO::WriteStructHeaderV020()
{
  throw std::runtime_error("ScancoHeaderIO::ReadHeader(std::ifstream&) not implemented.");
}

AIMV030StructHeader
AIMHeaderIO::WriteStructHeaderV030()
{
  throw std::runtime_error("ScancoHeaderIO::ReadHeader(std::ifstream&) not implemented.");
}

std::string
AIMHeaderIO::WriteProcessingLog()
{
  throw std::runtime_error("ScancoHeaderIO::ReadHeader(std::ifstream&) not implemented.");
}

size_t
AIMHeaderIO::WritePreHeader(std::ofstream & outfile, size_t imageSize, ScancoFileVersions version)
{
  throw std::runtime_error("ScancoHeaderIO::ReadHeader(std::ifstream&) not implemented.");
}

AIMHeaderIO::~AIMHeaderIO() {}

} // namespace itk
