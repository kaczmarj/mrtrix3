/*
   Copyright 2011 Brain Research Institute, Melbourne, Australia

   Written by Robert E. Smith, 2012.

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


#include "dwi/tractography/seeding/basic.h"
#include "dwi/tractography/rng.h"
#include "adapter/subset.h"


namespace MR
{
  namespace DWI
  {
    namespace Tractography
    {
      namespace Seeding
      {


        bool Sphere::get_seed (Eigen::Vector3f& p) const
        {
          std::uniform_real_distribution<float> uniform;
          do {
            p = { 2.0f*uniform(rng)-1.0f, 2.0f*uniform(rng)-1.0f, 2.0f*uniform(rng)-1.0f };
          } while (p.squaredNorm() > 1.0f);
          p = pos + rad*p;
          return true;
        }





        bool SeedMask::get_seed (Eigen::Vector3f& p) const
        {
          auto seed = mask;
          do {
            seed.index(0) = std::uniform_int_distribution<int>(0, mask.size(0)-1)(rng);
            seed.index(1) = std::uniform_int_distribution<int>(0, mask.size(1)-1)(rng);
            seed.index(2) = std::uniform_int_distribution<int>(0, mask.size(2)-1)(rng);
          } while (!seed.value());
          std::uniform_real_distribution<float> uniform;
          p = { seed.index(0)+uniform(rng)-0.5f, seed.index(1)+uniform(rng)-0.5f, seed.index(2)+uniform(rng)-0.5f };
          p = mask.voxel2scanner.cast<float>() * p;
          return true;
        }






        bool Random_per_voxel::get_seed (Eigen::Vector3f& p) const
        {

          if (expired)
            return false;

          std::lock_guard<std::mutex> lock (mutex);

          if (mask.index(2) < 0 || ++inc == num) {
            inc = 0;

            do {
              if (++mask.index(2) == mask.size(2)) {
                mask.index(2) = 0;
                if (++mask.index(1) == mask.size(1)) {
                  mask.index(1) = 0;
                  ++mask.index(0);
                }
              }
            } while (mask.index(0) != mask.size(0) && !mask.value());

            if (mask.index(0) == mask.size(0)) {
              expired = true;
              return false;
            }
          }

          std::uniform_real_distribution<float> uniform;
          p = { mask.index(0)+uniform(rng)-0.5f, mask.index(1)+uniform(rng)-0.5f, mask.index(2)+uniform(rng)-0.5f };
          p = mask.voxel2scanner.cast<float>() * p;
          return true;
        }








        bool Grid_per_voxel::get_seed (Eigen::Vector3f& p) const
        {

          if (expired)
            return false;

          std::lock_guard<std::mutex> lock (mutex);

          if (++pos[2] >= os) {
            pos[2] = 0;
            if (++pos[1] >= os) {
              pos[1] = 0;
              if (++pos[0] >= os) {
                pos[0] = 0;

                do {
                  if (++mask.index(2) == mask.size(2)) {
                    mask.index(2) = 0;
                    if (++mask.index(1) == mask.size(1)) {
                      mask.index(1) = 0;
                      ++mask.index(0);
                    }
                  }
                } while (mask.index(0) != mask.size(0) && !mask.value());
                if (mask.index(0) == mask.size(0)) {
                  expired = true;
                  return false;
                }
              }
            }
          }

          p = { mask.index(0)+offset+(pos[0]*step), mask.index(1)+offset+(pos[1]*step), mask.index(2)+offset+(pos[2]*step) };
          p = mask.voxel2scanner.cast<float>() * p;
          return true;

        }


        Rejection::Rejection (const std::string& in) :
          Base (in, "rejection sampling", MAX_TRACKING_SEED_ATTEMPTS_RANDOM),
#ifdef REJECTION_SAMPLING_USE_INTERPOLATION
          interp (in),
#endif
          max (0.0)
        {
          auto vox = Image<float>::open (in);
          std::vector<size_t> bottom (vox.ndim(), 0), top (vox.ndim(), 0);
          std::fill_n (bottom.begin(), 3, std::numeric_limits<size_t>::max());

          for (auto i = Loop (0,3) (vox); i; ++i) {
            const float value = vox.value();
            if (value) {
              if (value < 0.0)
                throw Exception ("Cannot have negative values in an image used for rejection sampling!");
              max = std::max (max, value);
              volume += value;
              if (size_t(vox.index(0)) < bottom[0]) bottom[0] = vox.index(0);
              if (size_t(vox.index(0)) > top[0])    top[0]    = vox.index(0);
              if (size_t(vox.index(1)) < bottom[1]) bottom[1] = vox.index(1);
              if (size_t(vox.index(1)) > top[1])    top[1]    = vox.index(1);
              if (size_t(vox.index(2)) < bottom[2]) bottom[2] = vox.index(2);
              if (size_t(vox.index(2)) > top[2])    top[2]    = vox.index(2);
            }
          }

          if (!max)
            throw Exception ("Cannot use image " + in + " for rejection sampling - image is empty");

          if (bottom[0]) --bottom[0];
          if (bottom[1]) --bottom[1];
          if (bottom[2]) --bottom[2];

          auto sub = Adapter::make<Adapter::Subset> (vox, bottom, top);
          Header header = sub;
          header.set_ndim (3);

          auto buf = Image<float>::scratch (header);
          volume *= buf.size(0) * buf.size(1) * buf.size(2);

          copy (sub, buf);
#ifdef REJECTION_SAMPLING_USE_INTERPOLATION
          interp = Interp::Linear<Image<float>> (buf);
#else
          image = buf;
          voxel2scanner = Transform (image).voxel2scanner;
#endif
        }



        bool Rejection::get_seed (Eigen::Vector3f& p) const
        {
          std::uniform_real_distribution<float> uniform;
#ifdef REJECTION_SAMPLING_USE_INTERPOLATION
          auto seed = interp;
          float selector;
          Eigen::Vector3f pos;
          do {
            pos = {
              uniform (rng) * (interp.size(0)-1), 
              uniform (rng) * (interp.size(1)-1), 
              uniform (rng) * (interp.size(2)-1) 
            };
            seed.voxel (pos);
            selector = rng.Uniform() * max;
          } while (seed.value() < selector);
          p = interp.voxel2scanner * pos;
#else
          auto seed = image;
          float selector;
          do {
            seed.index(0) = std::uniform_int_distribution<int> (0, image.size(0)-1) (rng);
            seed.index(1) = std::uniform_int_distribution<int> (0, image.size(1)-1) (rng);
            seed.index(2) = std::uniform_int_distribution<int> (0, image.size(2)-1) (rng);
            selector = uniform (rng) * max;
          } while (seed.value() < selector);
          p = { seed.index(0)+uniform(rng)-0.5f, seed.index(1)+uniform(rng)-0.5f, seed.index(2)+uniform(rng)-0.5f };
          p = voxel2scanner.cast<float>() * p;
#endif
          return true;
        }




      }
    }
  }
}


