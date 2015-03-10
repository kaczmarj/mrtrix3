/*
   Copyright 2009 Brain Research Institute, Melbourne, Australia

   Written by J-Donald Tournier, 18/08/09.

   This file is part of MRtrix.

   MRtrix is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   MRtrix is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef __image_io_base_h__
#define __image_io_base_h__

#include <vector>
#include <stdint.h>
#include <unistd.h>
#include <cassert>

#include "memory.h"
#include "mrtrix.h"
#include "file/entry.h"

#define MAX_FILES_PER_IMAGE 256U

namespace MR
{

  class Header;

  //! Classes responsible for actual image loading & writing
  /*! These classes are designed to provide a consistent interface for image
   * loading & writing, so that various non-trivial types of image storage
   * can be accommodated. These include compressed files, and images stored
   * as mosaic (e.g. Siemens DICOM mosaics). */
  namespace ImageIO
  {

    class Base
    {
      public:
        Base (const Header& header);
        Base (Base&&) noexcept = default;
        Base (const Base&) = delete;
        Base& operator=(const Base&) = delete;

        virtual ~Base ();

        virtual bool is_file_backed () const;

        // bits_per_element is only used for scratch data
        // it is ignored in all other (file-backed) handlers,
        // where the datatype in the header specifies the bits per element:
        void open (const Header& header, size_t bits_per_element = 0);
        void close (const Header& header);

        bool is_image_new () const { return is_new; }
        bool is_image_readwrite () const { return writable; }

        void set_readwrite (bool readwrite) {
          writable = readwrite;
        }
        void set_image_is_new (bool image_is_new) {
          is_new = image_is_new;
        }
        void set_readwrite_if_existing (bool readwrite) {
          if (!is_new) 
            writable = readwrite;
        }

        uint8_t* segment (size_t n) const {
          assert (n < addresses.size());
          return addresses[n].get();
        }
        size_t nsegments () const {
          return addresses.size();
        }
        size_t segment_size () const {
          check();
          return segsize;
        }

        std::vector<File::Entry> files;

        void merge (const Base& B) {
          assert (addresses.empty());
          for (size_t n = 0; n < B.files.size(); ++n) 
            files.push_back (B.files[n]);
          segsize += B.segsize;
        }

        friend std::ostream& operator<< (std::ostream& stream, const Base& B) {
          stream << str (B.files.size()) << " files, segsize " << str(B.segsize)
            << ", is " << (B.is_new ? "" : "NOT ") << "new, " << (B.writable ? "read/write" : "read-only");
          return stream;
        }

      protected:
        size_t segsize;
        std::vector<std::unique_ptr<uint8_t[]>> addresses;
        bool is_new, writable;

        void check () const {
          assert (addresses.size());
        }
        virtual void load (const Header& header, size_t bits_per_element) = 0;
        virtual void unload (const Header& header) = 0;
    };

  }

}

#endif
