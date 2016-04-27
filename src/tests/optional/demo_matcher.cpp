/// PROJECT
#include <ndt/data/pointcloud.hpp>
#include <ndt/matching/multi_grid_matcher_2D.hpp>
#include <ndt/visualization/multi_grid.hpp>
#include <ndt/visualization/points.hpp>

void linspace(const double min,
              const double max,
              const double res,
              std::vector<double> &values)
{
    const double        range = max - min;
    const std::size_t   intervals = range / res;
    for(std::size_t i = 0 ; i < intervals ; ++i) {
        values.push_back(min + res * i);
    }
}

typedef ndt::matching::MultiGridMatcher2D   MultiGridMatcher2D;
typedef ndt::visualization::MultiGrid2D     MultiGrid2D;
typedef ndt::visualization::Point2D         Point2D;

int main(int argc, char *argv[])
{
    std::vector<MultiGridMatcher2D::PointType> points_src;
    /// generate horizontal lines
    std::vector<double> xs;
    linspace(-10.0, -1.0, 0.1, xs);
    for(double &e : xs) {
        points_src.push_back(MultiGridMatcher2D::PointType(e, 1.0));
        points_src.push_back(MultiGridMatcher2D::PointType(e, -1.0));
    }
    /// generate vertial lines
    std::vector<double> ys;
    linspace(-10.0, 10.0, 0.1, ys);
    for(double &e : ys) {
        points_src.push_back(MultiGridMatcher2D::PointType(1.5, e));
        if(e < -1.0 || e > 1.0)
            points_src.push_back(MultiGridMatcher2D::PointType(-1.0, e));
    }
    /// generate a second points test which is transformed
    MultiGridMatcher2D::RotationType    rotation       = MultiGridMatcher2D::RotationType(0.0);
    MultiGridMatcher2D::TranslationType trans          = MultiGridMatcher2D::TranslationType(0.2, 0.0);
    MultiGridMatcher2D::TransformType   transformation = trans * rotation;
    std::vector<MultiGridMatcher2D::PointType> points_dst;
    for(MultiGridMatcher2D::PointType &p : points_src) {
        points_dst.push_back(transformation * p);
    }

    MultiGridMatcher2D::SizeType   size = {10, 10};
    MultiGridMatcher2D::ResolutionType resolution = {1.0, 1.0};
    ndt::data::Pointcloud<2> pointcloud_src(points_src);
    ndt::data::Pointcloud<2> pointcloud_dst(points_dst);

    /// show the point set
    cv::Mat display = cv::Mat(800, 800, CV_8UC3, cv::Scalar());
    ndt::visualization::renderPoints(points_src,
                                     size,
                                     resolution,
                                     display,
                                     cv::Scalar(255),
                                     false, 0.5);
    ndt::visualization::renderPoints(points_dst,
                                     size,
                                     resolution,
                                     display,
                                     cv::Scalar(0,255),
                                     false, 0.5);
    while(true) {
        cv::imshow("display", display);
        int key = cv::waitKey(0) & 0xFF;
        if(key == 27)
            break;
    }
    cv::flip(display, display, 0);

    /// now we can try out the matching
    MultiGridMatcher2D::Parameters params;
    params.max_iterations = 4000;
    MultiGridMatcher2D m(params);
    m.match(pointcloud_src, pointcloud_dst, transformation);

    for(MultiGridMatcher2D::PointType &p : points_dst) {
        p = transformation * p;
    }
    ndt::visualization::renderPoints(points_dst,
                                     size,
                                     resolution,
                                     display,
                                     cv::Scalar(0,0,255),
                                     false, 0.5);

    while(true) {
        cv::imshow("display", display);
        int key = cv::waitKey(0) & 0xFF;
        if(key == 27)
            break;
    }

    return 0;
}