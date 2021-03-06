#include "UnicvDbParser.h"

#include <string>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <iomanip>

#include <boost/filesystem.hpp>

#include "Utils.h"

uint32_t binTreeNumMaxAvail(uint32_t signatureSize, uint32_t pageSize)
{
  return (pageSize - sizeof(sig_tbl_header_t)) / signatureSize;
}

uint32_t binTreeSize(uint32_t signatureSize, uint32_t binTreeNumMaxAvail)
{
  return binTreeNumMaxAvail * signatureSize + sizeof(sig_tbl_header_t);
}

bool readSignatureBlock(std::ifstream& inputStream, scei_ftbl_header_t& ftHeader, uint32_t sizeCheck, sig_tbl_t& fdt)
{
   uint64_t cpo = inputStream.tellg();

   inputStream.read((char*)&fdt.dtHeader, sizeof(sig_tbl_header_t));
   
   if(fdt.dtHeader.binTreeSize != binTreeSize(0x14, ftHeader.binTreeNumMaxAvail))
   {
      std::cout << "Unexpected tableSize" << std::endl;
      return false;
   }

   //check to see if there are any other sizes
   if(fdt.dtHeader.sigSize != EXPECTED_SIGNATURE_SIZE)
   {
      std::cout << "Unexpected chunk size" << std::endl;
      return false;
   }

   //check padding
   if(fdt.dtHeader.padding != 0)
   {
      std::cout << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   //this check is usefull for validating file structure
   if(fdt.dtHeader.nSignatures != sizeCheck)
   {
      std::cout << "unexpected number of chunks" << std::endl;
      return false;
   }

   //read signatures
   for(uint32_t c = 0; c < fdt.dtHeader.nSignatures; c++)
   {
      fdt.signatures.push_back(std::vector<uint8_t>());
      std::vector<uint8_t>& dte = fdt.signatures.back();
      dte.resize(fdt.dtHeader.sigSize);
      inputStream.read((char*)dte.data(), fdt.dtHeader.sigSize);
   }

   //calculate size of tail data - this data should be zero padding
   //instead of skipping it is validated here that it contains only zeroes
   uint64_t cp = inputStream.tellg();
   int64_t tail = ftHeader.pageSize - (cp - cpo);

   std::vector<uint8_t> data(tail);
   inputStream.read((char*)data.data(), tail);

   if(!isZeroVector(data))
   {
      std::cout << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   //fast way would be to use seek
   //inputStream.seekg(tail, std::ios::cur);

   return true;
}

bool readDataBlock(std::ifstream& inputStream, uint64_t& i, scei_ftbl_t& fft)
{
   int64_t currentBlockPos = inputStream.tellg();

   inputStream.read((char*)&fft.ftHeader, sizeof(scei_ftbl_header_t));

   fft.page = currentBlockPos / fft.ftHeader.pageSize;  //off2page(currentBlockPos, fft.ftHeader.pageSize);

   //check that block size is expected
   //this will allow to fail if there are any other unexpected block sizes
   if(fft.ftHeader.pageSize != EXPECTED_PAGE_SIZE)
   {
      std::cout << "Unexpected block size" << std::endl;
      return false;
   }

   //check magic word
   if(std::string((const char*)fft.ftHeader.magic, 8) != FT_MAGIC_WORD)
   {
      std::cout << "Invalid magic word" << std::endl;
      return false;
   }

   //check version
   if(fft.ftHeader.version != UNICV_EXPECTED_VERSION_1 && fft.ftHeader.version != UNICV_EXPECTED_VERSION_2)
   {
      std::cout << "Unexpected version" << std::endl;
      return false;
   }

   //check maxNSectors
   if(fft.ftHeader.binTreeNumMaxAvail != binTreeNumMaxAvail(0x14, fft.ftHeader.pageSize))
   {
      std::cout << "Unexpected binTreeNumMaxAvail" << std::endl;
      return false;
   }

   //check file sector size
   if(fft.ftHeader.fileSectorSize != EXPECTED_FILE_SECTOR_SIZE)
   {
      std::cout << "Unexpected fileSectorSize" << std::endl;
      return false;
   }

   //check padding
   if(fft.ftHeader.padding != 0)
   {
      std::cout << "Unexpected padding" << std::endl;
      return false;
   }

   //calculate size of tail data - this data should be zero padding
   //instead of skipping it is validated here that it contains only zeroes
   uint64_t tail = fft.ftHeader.pageSize - sizeof(scei_ftbl_header_t);
   
   std::vector<uint8_t> tailData(tail);
   inputStream.read((char*)tailData.data(), tail);

   if(!isZeroVector(tailData))
   {
      std::cout << "Unexpected data instead of padding" << std::endl;
      return false;
   }

   //fast way would be to use seek
   //inputStream.seekg(tail, std::ios::cur);

   //check if there are any data blocks after current entry
   if(fft.ftHeader.nSectors == 0)
      return true;

   //check if there is single block read required or multiple
   if(fft.ftHeader.nSectors < fft.ftHeader.binTreeNumMaxAvail)
   {
      //create new signature block
      fft.blocks.push_back(sig_tbl_t());
      sig_tbl_t& fdt = fft.blocks.back();

      //read and valiate signature block
      if(!readSignatureBlock(inputStream, fft.ftHeader, fft.ftHeader.nSectors, fdt))
         return false;

      i++;
      return true;
   }
   else
   {
      uint32_t nDataBlocks = fft.ftHeader.nSectors / fft.ftHeader.binTreeNumMaxAvail;
      uint32_t nDataTail = fft.ftHeader.nSectors % fft.ftHeader.binTreeNumMaxAvail;

      for(uint32_t dbi = 0; dbi < nDataBlocks; dbi++)
      {
         //create new signature block
         fft.blocks.push_back(sig_tbl_t());
         sig_tbl_t& fdt = fft.blocks.back();

         //read and valiate signature block
         if(!readSignatureBlock(inputStream, fft.ftHeader, fft.ftHeader.binTreeNumMaxAvail, fdt))
            return false;
         i++;
      }

      if(nDataTail > 0)
      {
         //create new signature block
         fft.blocks.push_back(sig_tbl_t());
         sig_tbl_t& fdt = fft.blocks.back();

         //read and valiate signature block
         if(!readSignatureBlock(inputStream, fft.ftHeader, nDataTail, fdt))
            return false;
         i++;
      }

      return true;
   }
}

bool parseUnicvDb(std::ifstream& inputStream, scei_rodb_t& fdb)
{
   //get stream size
   inputStream.seekg(0, std::ios::end);
   uint64_t fileSize = inputStream.tellg();
   inputStream.seekg(0, std::ios::beg);
   
   //read header
   inputStream.read((char*)&fdb.dbHeader, sizeof(scei_rodb_header_t));

   //check file size field
   if(fileSize != (fdb.dbHeader.dataSize + fdb.dbHeader.blockSize)) //do not forget to count header
   {
      std::cout << "Incorrect block size or data size" << std::endl;
      return false;
   }

   //check magic word
   if(std::string((const char*)fdb.dbHeader.magic, 8) != DB_MAGIC_WORD)
   {
      std::cout << "Invalid magic word" << std::endl;
      return false;
   }

   //check version
   if(fdb.dbHeader.version != UNICV_EXPECTED_VERSION_1 && fdb.dbHeader.version != UNICV_EXPECTED_VERSION_2)
   {
      std::cout << "Unexpected version" << std::endl;
      return false;
   }

   if(fdb.dbHeader.unk2 != 0xFFFFFFFF)
   {
      std::cout << "Unexpected unk2" << std::endl;
      return false;
   }

   if(fdb.dbHeader.unk3 != 0xFFFFFFFF)
   {
      std::cout << "Unexpected unk3" << std::endl;
      return false;
   }

   //debug check only for now to see if there are any other sizes
   if(fdb.dbHeader.blockSize != EXPECTED_PAGE_SIZE)
   {
      std::cout << "Unexpected page size" << std::endl;
      return false;
   }

   inputStream.seekg(fdb.dbHeader.blockSize, std::ios::beg); //skip header

   //it looks like unicv file is split into groups of SCEIFTBL chunks (blocks)
   //where each group corresponds to file or directory
   //however there is no obvious way to determine number of chunks in each group

   //the only way is to calculate total number of chunks (blocks)
   //and read them as stream splitting it into groups in the process
   
   uint64_t nBlocks = fdb.dbHeader.dataSize / fdb.dbHeader.blockSize;
   uint64_t tailSize = fdb.dbHeader.dataSize % fdb.dbHeader.blockSize;

   //check tail size just in case
   if(tailSize > 0)
   {
      std::cout << "Block misalign" << std::endl;
      return false;
   }

   std::cout << "Total blocks: " << std::dec << nBlocks << std::endl;

   //read all blocks
   for(uint64_t i = 0; i < nBlocks; i++)
   {
      fdb.tables.push_back(scei_ftbl_t());
      scei_ftbl_t& fft = fdb.tables.back();

      if(!readDataBlock(inputStream, i, fft))
         return false;
   }

   //check that there is no data left
   uint64_t endp = inputStream.tellg();
   if(fileSize != endp)
   {
      std::cout << "Data misalign" << std::endl;
      return false;
   }

   return true;
}

int parseUnicvDb(boost::filesystem::path titleIdPath, scei_rodb_t& fdb)
{
   std::cout << "parsing  unicv.db..." << std::endl;

   boost::filesystem::path root(titleIdPath);

   boost::filesystem::path filepath = root / "sce_pfs" / "unicv.db";
   if(!boost::filesystem::exists(filepath))
   {
      std::cout << "failed to find unicv.db file" << std::endl;
      return -1;
   }

   std::ifstream inputStream(filepath.generic_string().c_str(), std::ios::in | std::ios::binary);

   if(!inputStream.is_open())
   {
      std::cout << "failed to open unicv.db file" << std::endl;
      return -1;
   }

   if(!parseUnicvDb(inputStream, fdb))
      return -1;

   return 0;
}
