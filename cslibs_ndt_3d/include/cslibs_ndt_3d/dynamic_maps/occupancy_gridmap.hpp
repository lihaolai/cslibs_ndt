#ifndef CSLIBS_NDT_3D_DYNAMIC_OCCUPANCY_GRIDMAP_HPP
#define CSLIBS_NDT_3D_DYNAMIC_OCCUPANCY_GRIDMAP_HPP

#include <array>
#include <vector>
#include <cmath>
#include <memory>

#include <cslibs_math_3d/linear/pose.hpp>
#include <cslibs_math_3d/linear/point.hpp>

#include <cslibs_ndt/common/occupancy_distribution.hpp>
#include <cslibs_ndt/common/bundle.hpp>

#include <cslibs_math/linear/pointcloud.hpp>
#include <cslibs_math/common/array.hpp>
#include <cslibs_math/common/div.hpp>
#include <cslibs_math/common/mod.hpp>

#include <cslibs_indexed_storage/storage.hpp>
#include <cslibs_indexed_storage/backend/kdtree/kdtree.hpp>

#include <cslibs_math_3d/algorithms/bresenham.hpp>
#include <cslibs_math_3d/algorithms/simple_iterator.hpp>
#include <cslibs_math_3d/algorithms/efla_iterator.hpp>

#include <unordered_map>
namespace cis = cslibs_indexed_storage;

namespace cslibs_ndt_3d {
namespace dynamic_maps {
class OccupancyGridmap
{
public:
    using Ptr                               = std::shared_ptr<OccupancyGridmap>;
    using pose_t                            = cslibs_math_3d::Pose3d;
    using transform_t                       = cslibs_math_3d::Transform3d;
    using point_t                           = cslibs_math_3d::Point3d;
    using index_t                           = std::array<int, 3>;
    using mutex_t                           = std::mutex;
    using lock_t                            = std::unique_lock<mutex_t>;
    using distribution_t                    = cslibs_ndt::OccupancyDistribution<3>;
    using distribution_storage_t            = cis::Storage<distribution_t, index_t, cis::backend::kdtree::KDTree>;
    using distribution_storage_ptr_t        = std::shared_ptr<distribution_storage_t>;
    using distribution_storage_array_t      = std::array<distribution_storage_ptr_t, 8>;
    using distribution_bundle_t             = cslibs_ndt::Bundle<distribution_t*, 8>;
    using distribution_const_bundle_t       = cslibs_ndt::Bundle<const distribution_t*, 8>;
    using distribution_bundle_storage_t     = cis::Storage<distribution_bundle_t, index_t, cis::backend::kdtree::KDTree>;
    using distribution_bundle_storage_ptr_t = std::shared_ptr<distribution_bundle_storage_t>;
    using simple_iterator_t                 = cslibs_math_3d::algorithms::SimpleIterator;
    using inverse_sensor_model_t            = cslibs_gridmaps::utility::InverseModel;

    OccupancyGridmap(const pose_t &origin,
                     const double  resolution) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        bundle_resolution_(0.5 * resolution_),
        bundle_resolution_inv_(1.0 / bundle_resolution_),
        w_T_m_(origin),
        m_T_w_(w_T_m_.inverse()),
        min_index_{{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}},
        max_index_{{std::numeric_limits<int>::min(), std::numeric_limits<int>::min()}},
        storage_{{distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t),
                 distribution_storage_ptr_t(new distribution_storage_t)}},
        bundle_storage_(new distribution_bundle_storage_t)
    {
    }

    OccupancyGridmap(const pose_t &origin,
                     const double  resolution,
                     const std::shared_ptr<distribution_bundle_storage_t> &bundles,
                     const distribution_storage_array_t                   &storage,
                     const index_t                                        &min_index,
                     const index_t                                        &max_index) :
        resolution_(resolution),
        resolution_inv_(1.0 / resolution_),
        bundle_resolution_(0.5 * resolution_),
        bundle_resolution_inv_(1.0 / bundle_resolution_),
        w_T_m_(origin),
        m_T_w_(w_T_m_.inverse()),
        min_index_(min_index),
        max_index_(max_index),
        storage_(storage),
        bundle_storage_(bundles)
    {
    }

    inline point_t getMin() const
    {
        lock_t l(bundle_storage_mutex_);
        return point_t(min_index_[0] * bundle_resolution_,
                       min_index_[1] * bundle_resolution_,
                       min_index_[2] * bundle_resolution_);
    }

    inline point_t getMax() const
    {
        lock_t l(bundle_storage_mutex_);
        return point_t((max_index_[0] + 1) * bundle_resolution_,
                       (max_index_[1] + 1) * bundle_resolution_,
                       (max_index_[2] + 1) * bundle_resolution_);
    }

    inline pose_t getOrigin() const
    {
        cslibs_math_3d::Transform3d origin = w_T_m_;
        origin.translation() = getMin();
        return origin;
    }

    inline pose_t getInitialOrigin() const
    {
        return w_T_m_;
    }

    template <typename line_iterator_t = simple_iterator_t>
    inline void add(const point_t &start_p,
                    const point_t &end_p)
    {
        const index_t &end_index = toBundleIndex(end_p);
        updateOccupied(end_index, end_p);

        line_iterator_t it(m_T_w_ * start_p, m_T_w_ * end_p, bundle_resolution_);
        while (!it.done()) {
            updateFree({{it.x(), it.y(), it.z()}});
            ++ it;
        }
    }

    template <typename line_iterator_t = simple_iterator_t>
    inline void add(const point_t &start_p,
                    const point_t &end_p,
                    index_t       &end_index)
    {
        end_index = toBundleIndex(end_p);
        updateOccupied(end_index, end_p);

        line_iterator_t it(m_T_w_ * start_p, m_T_w_ * end_p, bundle_resolution_);
        while (!it.done()) {
            updateFree({{it.x(), it.y(), it.z()}});
            ++ it;
        }
    }

    template <typename line_iterator_t = simple_iterator_t>
    inline void insert(const pose_t &origin,
                       const typename cslibs_math::linear::Pointcloud<point_t>::Ptr &points)
    {
        distribution_storage_t storage;
        for (const auto &p : *points) {
            const point_t pm = origin * p;
            if (pm.isNormal()) {
                const index_t &bi = toBundleIndex(pm);
                distribution_t *d = storage.get(bi);
                (d ? d : &storage.insert(bi, distribution_t()))->updateOccupied(pm);
            }
        }

        const point_t start_p = m_T_w_ * origin.translation();
        storage.traverse([this, &start_p](const index_t& bi, const distribution_t &d) {
            if (!d.getDistribution())
                return;
            updateOccupied(bi, d.getDistribution());

            line_iterator_t it(start_p, m_T_w_ * point_t(d.getDistribution()->getMean()), bundle_resolution_);
            const std::size_t n = d.numOccupied();
            while (!it.done()) {
                updateFree({{it.x(), it.y(), it.z()}}, n);
                ++ it;
            }
        });
    }

    template <typename line_iterator_t = simple_iterator_t>
    inline void insertVolumetric(const pose_t &origin,
                                 const typename cslibs_math::linear::Pointcloud<point_t>::Ptr &points,
                                 const inverse_sensor_model_t::Ptr &ivm,
                                 const inverse_sensor_model_t::Ptr &ivm_visibility)
    {
        const index_t start_bi = toBundleIndex(origin.translation());
        auto occupancy = [this, &ivm](const index_t &bi) {
            const distribution_bundle_t *bundle = getDistributionBundle(bi);
            return 0.125 * (bundle->at(0)->getOccupancy(ivm) +
                            bundle->at(1)->getOccupancy(ivm) +
                            bundle->at(2)->getOccupancy(ivm) +
                            bundle->at(3)->getOccupancy(ivm) +
                            bundle->at(4)->getOccupancy(ivm) +
                            bundle->at(5)->getOccupancy(ivm) +
                            bundle->at(6)->getOccupancy(ivm) +
                            bundle->at(7)->getOccupancy(ivm));
        };
        auto current_visibility = [this, &start_bi, &ivm_visibility, &occupancy](const index_t &bi) {
            const double occlusion_prob =
                    std::min(occupancy({{bi[0] + ((bi[0] > start_bi[0]) ? 1 : -1), bi[1], bi[2]}}),
                             std::min(occupancy({{bi[0], bi[1] + ((bi[1] > start_bi[1]) ? 1 : -1), bi[2]}}),
                                      occupancy({{bi[0], bi[1], bi[2] + ((bi[2] > start_bi[2]) ? 1 : -1)}})));
            return ivm_visibility->getProbFree() * occlusion_prob +
                   ivm_visibility->getProbOccupied() * (1.0 - occlusion_prob);
        };

        distribution_storage_t storage;
        for (const auto &p : *points) {
            const point_t pm = origin * p;
            if (pm.isNormal()) {
                const index_t &bi = toBundleIndex(pm);
                distribution_t *d = storage.get(bi);
                (d ? d : &storage.insert(bi, distribution_t()))->updateOccupied(pm);
            }
        }

        const point_t start_p = m_T_w_ * origin.translation();
        storage.traverse([this, &ivm_visibility, &start_p, &current_visibility](const index_t& bi, const distribution_t &d) {
            if (!d.getDistribution())
                return;

            const point_t end_p = m_T_w_ * point_t(d.getDistribution()->getMean());
            line_iterator_t it(start_p, end_p, bundle_resolution_);

            const std::size_t n = d.numOccupied();
            double visibility = 1.0;
            while (!it.done()) {
                const index_t bit = {{it.x(), it.y(), it.z()}};
                std::cout << visibility << std::endl;
                if ((visibility *= current_visibility(bit)) < ivm_visibility->getProbPrior())
                    return;

                updateFree(bit, n);
                ++ it;
            }

            if ((visibility *= current_visibility(bi)) >= ivm_visibility->getProbPrior())
                updateOccupied(bi, d.getDistribution());
        });
    }

    inline double sample(const point_t &p,
                         const inverse_sensor_model_t::Ptr &ivm) const
    {
        const index_t bi = toBundleIndex(p);
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = bundle_storage_->get(bi);
        }

        auto sample = [this, &ivm] (const distribution_t *d,
                              const point_t &p) {
            return (d && d->getDistribution()) ?
                        (d->getDistribution()->sample(p) * d->getOccupancy(ivm)) : 0.0;
        };
        auto evaluate = [&sample] (const distribution_bundle_t * b,
                                   const point_t &p) {
            return 0.125 * (sample(b->at(0), p) +
                            sample(b->at(1), p) +
                            sample(b->at(2), p) +
                            sample(b->at(3), p) +
                            sample(b->at(4), p) +
                            sample(b->at(5), p) +
                            sample(b->at(6), p) +
                            sample(b->at(7), p));
        };

        return bundle ? evaluate(bundle, p) : 0.0;
    }


    inline double sampleNonNormalized(const point_t &p,
                                      const inverse_sensor_model_t::Ptr &ivm) const
    {
        const index_t bi = toBundleIndex(p);
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = bundle_storage_->get(bi);
        }

        auto sample = [this, &ivm] (const distribution_t *d,
                              const point_t &p) {
            return (d && d->getDistribution()) ?
                        (d->getDistribution()->sampleNonNormalized(p) * d->getOccupancy(ivm)) : 0.0;
        };
        auto evaluate = [&sample] (const distribution_bundle_t * b,
                                   const point_t &p) {
            return 0.125 * (sample(b->at(0), p) +
                            sample(b->at(1), p) +
                            sample(b->at(2), p) +
                            sample(b->at(3), p) +
                            sample(b->at(4), p) +
                            sample(b->at(5), p) +
                            sample(b->at(6), p) +
                            sample(b->at(7), p));
        };

        return bundle ? evaluate(bundle, p) : 0.0;
    }

    inline index_t getMinDistributionIndex() const
    {
        lock_t l(storage_mutex_);
        return min_index_;
    }

    inline index_t getMaxDistributionIndex() const
    {
        lock_t l(storage_mutex_);
        return max_index_;
    }

    inline const distribution_bundle_t* getDistributionBundle(const index_t &bi) const
    {
        return getAllocate(bi);
    }

    inline distribution_bundle_t* getDistributionBundle(const index_t &bi)
    {
        return getAllocate(bi);
    }

    inline double getBundleResolution() const
    {
        return bundle_resolution_;
    }

    inline double getResolution() const
    {
        return resolution_;
    }

    inline double getHeight() const
    {
        return (max_index_[1] - min_index_[1] + 1) * bundle_resolution_;
    }

    inline double getWidth() const
    {
        return (max_index_[0] - min_index_[0] + 1) * bundle_resolution_;
    }

    inline distribution_storage_array_t const & getStorages() const
    {
        return storage_;
    }

    inline void getBundleIndices(std::vector<index_t> &indices) const
    {
        lock_t(bundle_storage_mutex_);
        auto add_index = [&indices](const index_t &i, const distribution_bundle_t &d) {
            indices.emplace_back(i);
        };
        bundle_storage_->traverse(add_index);
    }

private:
    const double                                    resolution_;
    const double                                    resolution_inv_;
    const double                                    bundle_resolution_;
    const double                                    bundle_resolution_inv_;
    const transform_t                               w_T_m_;
    const transform_t                               m_T_w_;

    mutable index_t                                 min_index_;
    mutable index_t                                 max_index_;
    mutable mutex_t                                 storage_mutex_;
    mutable distribution_storage_array_t            storage_;
    mutable mutex_t                                 bundle_storage_mutex_;
    mutable distribution_bundle_storage_ptr_t       bundle_storage_;

    inline distribution_t* getAllocate(const distribution_storage_ptr_t &s,
                                       const index_t &i) const
    {
        lock_t l(storage_mutex_);
        distribution_t *d = s->get(i);
        return d ? d : &(s->insert(i, distribution_t()));
    }

    inline distribution_bundle_t *getAllocate(const index_t &bi) const
    {
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = bundle_storage_->get(bi);
        }

        auto allocate_bundle = [this, &bi]() {
            distribution_bundle_t b;

            const int divx = cslibs_math::common::div<int>(bi[0], 2);
            const int divy = cslibs_math::common::div<int>(bi[1], 2);
            const int divz = cslibs_math::common::div<int>(bi[2], 2);
            const int modx = cslibs_math::common::mod<int>(bi[0], 2);
            const int mody = cslibs_math::common::mod<int>(bi[1], 2);
            const int modz = cslibs_math::common::mod<int>(bi[2], 2);

            const index_t storage_index_0 = {{divx,        divy,        divz}};
            const index_t storage_index_1 = {{divx + modx, divy,        divz}};
            const index_t storage_index_2 = {{divx,        divy + mody, divz}};
            const index_t storage_index_3 = {{divx + modx, divy + mody, divz}};
            const index_t storage_index_4 = {{divx,        divy,        divz + modz}};
            const index_t storage_index_5 = {{divx + modx, divy,        divz + modz}};
            const index_t storage_index_6 = {{divx,        divy + mody, divz + modz}};
            const index_t storage_index_7 = {{divx + modx, divy + mody, divz + modz}};

            b[0] = getAllocate(storage_[0], storage_index_0);
            b[1] = getAllocate(storage_[1], storage_index_1);
            b[2] = getAllocate(storage_[2], storage_index_2);
            b[3] = getAllocate(storage_[3], storage_index_3);
            b[4] = getAllocate(storage_[4], storage_index_4);
            b[5] = getAllocate(storage_[5], storage_index_5);
            b[6] = getAllocate(storage_[6], storage_index_6);
            b[7] = getAllocate(storage_[7], storage_index_7);

            lock_t(bundle_storage_mutex_);
            updateIndices(bi);
            return &(bundle_storage_->insert(bi, b));
        };

        return bundle == nullptr ? allocate_bundle() : bundle;
    }

    inline void updateFree(const index_t &bi) const
    {
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = getAllocate(bi);
        }
        bundle->at(0)->updateFree();
        bundle->at(1)->updateFree();
        bundle->at(2)->updateFree();
        bundle->at(3)->updateFree();
        bundle->at(4)->updateFree();
        bundle->at(5)->updateFree();
        bundle->at(6)->updateFree();
        bundle->at(7)->updateFree();
    }

    inline void updateFree(const index_t &bi,
                           const std::size_t &n) const
    {
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = getAllocate(bi);
        }
        bundle->at(0)->updateFree(n);
        bundle->at(1)->updateFree(n);
        bundle->at(2)->updateFree(n);
        bundle->at(3)->updateFree(n);
        bundle->at(4)->updateFree(n);
        bundle->at(5)->updateFree(n);
        bundle->at(6)->updateFree(n);
        bundle->at(7)->updateFree(n);
    }

    inline void updateOccupied(const index_t &bi,
                               const point_t &p) const
    {
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = getAllocate(bi);
        }
        bundle->at(0)->updateOccupied(p);
        bundle->at(1)->updateOccupied(p);
        bundle->at(2)->updateOccupied(p);
        bundle->at(3)->updateOccupied(p);
        bundle->at(4)->updateOccupied(p);
        bundle->at(5)->updateOccupied(p);
        bundle->at(6)->updateOccupied(p);
        bundle->at(7)->updateOccupied(p);
    }

    inline void updateOccupied(const index_t &bi,
                               const distribution_t::distribution_ptr_t &d) const
    {
        distribution_bundle_t *bundle;
        {
            lock_t(bundle_storage_mutex_);
            bundle = getAllocate(bi);
        }
        bundle->at(0)->updateOccupied(d);
        bundle->at(1)->updateOccupied(d);
        bundle->at(2)->updateOccupied(d);
        bundle->at(3)->updateOccupied(d);
        bundle->at(4)->updateOccupied(d);
        bundle->at(5)->updateOccupied(d);
        bundle->at(6)->updateOccupied(d);
        bundle->at(7)->updateOccupied(d);
    }

    inline void updateIndices(const index_t &bi) const
    {
        min_index_ = std::min(min_index_, bi);
        max_index_ = std::max(max_index_, bi);
    }

    inline index_t toBundleIndex(const point_t &p_w) const
    {
        const point_t p_m = m_T_w_ * p_w;
        return {{static_cast<int>(std::floor(p_m(0) * bundle_resolution_inv_)),
                 static_cast<int>(std::floor(p_m(1) * bundle_resolution_inv_)),
                 static_cast<int>(std::floor(p_m(2) * bundle_resolution_inv_))}};
    }
};
}
}

#endif // CSLIBS_NDT_3D_DYNAMIC_OCCUPANCY_GRIDMAP_HPP
