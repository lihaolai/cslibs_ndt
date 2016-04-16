#include <ros/ros.h>
#include <sensor_msgs/LaserScan.h>

#include "../optional/visualize.hpp"
#include "../ndt/multi_grid.hpp"
#include "../data/laserscan.hpp"
#include "../convert/convert.hpp"

namespace ndt {
struct ScanVisualizerNode {
    typedef ndt::NDTMultiGrid<2> NDTGridType;

    ros::NodeHandle   nh;
    ros::Subscriber   sub;
    double            resolution;

    ScanVisualizerNode() :
        nh("~"),
        resolution(1.0)
    {
        std::string topic = "/scan";
        nh.getParam("resolution", resolution);
        nh.getParam("topic", topic);

        sub = nh.subscribe<sensor_msgs::LaserScan>(topic, 1, &ScanVisualizerNode::laserscan, this);

    }

    void laserscan(const sensor_msgs::LaserScanConstPtr &msg)
    {
        data::LaserScan scan;
        convert::convert(msg, scan);
        /// make a grid
        data::LaserScan::PointType range = scan.max - scan.min;
        NDTGridType::Size       size = {static_cast<std::size_t>(range(0) / resolution),
                                        static_cast<std::size_t>(range(1) / resolution)};
        NDTGridType::Resolution res = {resolution, resolution};

        ndt::NDTMultiGrid2D grid(size, res, scan.min);
        for(std::size_t i = 0 ; i < scan.size ; ++i) {
            if(scan.mask[i] == data::LaserScan::VALID) {
                if(!grid.add(scan.points[i]))
                    std::cerr << "Failed to add point [" << scan.points[i] << "]" << std::endl;
            }
        }
        /// render the grid
        cv::Mat display(500,500, CV_8UC3, cv::Scalar());
        ndt::renderNDTGrid(grid, scan.min, scan.max, display);
        cv::imshow("ndt", display);
        cv::waitKey(19);
    }

};
}

int main(int argc, char *argv[])
{
    ros::init(argc, argv, "ndt_visualization_node");
    ndt::ScanVisualizerNode sn;
    ros::spin();

    return 0;
}