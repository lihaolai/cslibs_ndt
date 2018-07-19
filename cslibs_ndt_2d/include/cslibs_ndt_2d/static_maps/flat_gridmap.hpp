#ifndef CSLIBS_NDT_2D_STATIC_MAPS_FLAT_GRIDMAP_HPP
#define CSLIBS_NDT_2D_STATIC_MAPS_FLAT_GRIDMAP_HPP

#include <array>
#include <vector>
#include <cmath>
#include <memory>

#include <cslibs_math_2d/linear/pose.hpp>
#include <cslibs_math_2d/linear/point.hpp>

#include <cslibs_ndt/common/distribution.hpp>
#include <cslibs_ndt/common/bundle.hpp>

#include <cslibs_math/linear/pointcloud.hpp>
#include <cslibs_math/common/array.hpp>
#include <cslibs_math/common/div.hpp>
#include <cslibs_math/common/mod.hpp>

#include <cslibs_indexed_storage/storage.hpp>
#include <cslibs_indexed_storage/backend/array/array.hpp>

namespace cis = cslibs_indexed_storage;

namespace cslibs_ndt_2d {
namespace static_maps {
namespace flat {
class Gridmap
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    using allocator_t = Eigen::aligned_allocator<Gridmap>;

    using Ptr                               = std::shared_ptr<Gridmap>;
    using pose_t                            = cslibs_math_2d::Pose2d;
    using transform_t                       = cslibs_math_2d::Transform2d;
    using point_t                           = cslibs_math_2d::Point2d;
    using index_t                           = std::array<int, 2>;
    using size_t                            = std::array<std::size_t, 2>;
    using size_m_t                          = std::array<double, 2>;
    using mutex_t                           = std::mutex;
    using lock_t                            = std::unique_lock<mutex_t>;
    using distribution_t                    = cslibs_ndt::Distribution<2>;
    using distribution_storage_t            = cis::Storage<distribution_t, index_t, cis::backend::array::Array>;
    using distribution_storage_ptr_t        = std::shared_ptr<distribution_storage_t>;
    using distribution_storage_const_ptr_t  = std::shared_ptr<distribution_storage_t const>;

    Gridmap(const pose_t &origin,
            const double &resolution,
            const size_t &size) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        w_T_m_(origin),
        m_T_w_(w_T_m_.inverse()),
        size_(size),
        size_m_{{(size[0] + 1) * resolution,
                 (size[1] + 1) * resolution}},
        storage_(distribution_storage_ptr_t(new distribution_storage_t))
    {
      storage_->template set<cis::option::tags::array_size>(size[0], size[1]);
    }

    Gridmap(const double &origin_x,
            const double &origin_y,
            const double &origin_phi,
            const double &resolution,
            const size_t &size) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        w_T_m_(origin_x, origin_y, origin_phi),
        m_T_w_(w_T_m_.inverse()),
        size_(size),
        size_m_{{(size[0] + 1) * resolution,
                 (size[1] + 1) * resolution}},
        storage_(distribution_storage_ptr_t(new distribution_storage_t))
    {
        storage_->template set<cis::option::tags::array_size>(size[0], size[1]);
    }

    /**
     * @brief Get minimum in map coordinates.
     * @return the minimum
     */
    inline point_t getMin() const
    {
        lock_t(bundle_storage_mutex_);
        return point_t();
    }

    /**
     * @brief Get maximum in map coordinates.
     * @return the maximum
     */
    inline point_t getMax() const
    {
        lock_t(bundle_storage_mutex_);
        return point_t(size_m_[0],size_m_[1]);
    }

    /**
     * @brief Get the origin.
     * @return the origin
     */
    inline pose_t getOrigin() const
    {
        cslibs_math_2d::Transform2d origin = w_T_m_;
        return origin;
    }

    inline void add(const point_t &p)
    {
        distribution_t *distribution;
        {
            const index_t i = toIndex(p);
            lock_t(storage_mutex_);
            distribution = getAllocate(i);
        }
        distribution->getHandle()->data().add(p);
    }

    inline double sample(const point_t &p) const
    {
        const index_t i = toIndex(p);
        return sample(p, i);
    }

    inline double sample(const point_t &p,
                         const index_t &i) const
    {
        distribution_t *distribution;
        {
            lock_t(storage_mutex_);
            distribution = storage_->get(i);
        }
        return distribution ? distribution->getHandle()->data().sample(p) : 0.0;
    }

    inline double sampleNonNormalized(const point_t &p) const
    {
        const index_t i = toIndex(p);
        return sampleNonNormalized(p, i);
    }

    inline double sampleNonNormalized(const point_t &p,
                                      const index_t &i) const
    {
        distribution_t *distribution;
        {
            lock_t(storage_mutex_);
            distribution = storage_->get(i);
        }
        return distribution ? distribution->getHandle()->data().sampleNonNormalized(p) : 0.0;
    }

    inline distribution_t* get(const point_t &p) const
    {
        const index_t i = toIndex(p);
        distribution_t *distribution;
        {
            lock_t(storage_mutex_);
            distribution = storage_->get(i);
        }
        return distribution;
    }


    inline const distribution_t* getDistribution(const index_t &i) const
    {
        return getAllocate(i);
    }

    inline distribution_t* getDistribution(const index_t &i)
    {
        return getAllocate(i);
    }

    inline double getResolution() const
    {
        return resolution_;
    }

    inline double getHeight() const
    {
        return size_[1] * resolution_;
    }

    inline double getWidth() const
    {
        return size_[0] * resolution_;
    }

    inline size_t getSize() const
    {
        return size_;
    }

    template <typename Fn>
    inline void traverse(const Fn& function) const
    {
        lock_t(storage_mutex_);
        return storage_->traverse(function);
    }

    inline void getIndices(std::vector<index_t> &indices) const
    {
        lock_t(storage_mutex_);
        auto add_index = [&indices](const index_t &i, const distribution_t &) {
            indices.emplace_back(i);
        };
        storage_->traverse(add_index);
    }

    inline std::size_t getByteSize() const
    {
        lock_t(storage_mutex_);
        return sizeof(*this) +
                storage_->byte_size();
    }

    inline virtual bool validate(const pose_t &p_w) const
    {
      const point_t p_m = m_T_w_ * p_w.translation();
      return p_m(0) >= 0.0 && p_m(0) < size_m_[0] &&
             p_m(1) >= 0.0 && p_m(1) < size_m_[1];
    }

protected:
    const double                                    resolution_;
    const double                                    resolution_inv_;
    const transform_t                               w_T_m_;
    const transform_t                               m_T_w_;
    const size_t                                    size_;
    const size_m_t                                  size_m_;

    mutable mutex_t                                 storage_mutex_;
    mutable distribution_storage_ptr_t              storage_;

    inline distribution_t* getAllocate(const distribution_storage_ptr_t &s,
                                       const index_t &i) const
    {
        lock_t(storage_mutex_);
        distribution_t *d = s->get(i);
        return d ? d : &(s->insert(i, distribution_t()));
    }

    inline distribution_t *getAllocate(const index_t &i) const
    {
        distribution_t *distribution;
        {
            lock_t(storage_mutex_);
            distribution = storage_->get(i);
        }
        auto allocate = [this, &i]() {
            lock_t(storage_mutex_);
            return &(storage_->insert(i, distribution_t()));
        };
        return distribution ? distribution : allocate();
    }

    inline index_t toIndex(const point_t &p_w) const
    {
        const point_t p_m = m_T_w_ * p_w;
        return {{static_cast<int>(std::floor(p_m(0) * resolution_inv_)),
                 static_cast<int>(std::floor(p_m(1) * resolution_inv_))}};
    }

    inline void fromIndex(const index_t &i,
                          point_t &p_w) const
    {
        p_w = w_T_m_ * point_t(i[0] * resolution_,
                               i[1] * resolution_);
    }
};
}
}
}

#endif // CSLIBS_NDT_2D_STATIC_MAPS_FLAT_GRIDMAP_HPP
