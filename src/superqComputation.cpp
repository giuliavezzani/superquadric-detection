/*
 * Copyright (C) 2015 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Giulia Vezzani
 * email:  giulia.vezzani@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <cmath>
#include <algorithm>
#include <sstream>
#include <set>
#include <fstream>

#include <yarp/math/Math.h>
#include <yarp/math/SVD.h>

#include "superqComputation.h"

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;
using namespace yarp::sig;
using namespace yarp::math;
using namespace iCub::ctrl;

/*******************************************************************************/
vector<int>  SpatialDensityFilter::filter(const cv::Mat &data,const double radius, const int maxResults, deque<Vector> &points)
{
    deque<Vector> ind;
    cv::flann::KDTreeIndexParams indexParams;
    cv::flann::Index kdtree(data,indexParams);

    cv::Mat query(1,data.cols,CV_32F);
    cv::Mat indices,dists;

    vector<int> res(data.rows);

    for (size_t i=0; i<res.size(); i++)
    {
        for (int c=0; c<query.cols; c++)
            query.at<float>(0,c)=data.at<float>(i,c);

        res[i]=kdtree.radiusSearch(query,indices,dists,radius,maxResults,cv::flann::SearchParams(128));

        Vector point(3,0.0);
        if (res[i]>=maxResults)
        {
            point[0]=data.at<float>(i,0);
            point[1]=data.at<float>(i,1);
            point[2]=data.at<float>(i,2);
            points.push_back(point);
        }
    }

    return res;
}

/***********************************************************************/
SuperqComputation::SuperqComputation(Mutex &_mutex_shared, int _rate, bool _filter_points, bool _filter_superq, bool _single_superq, bool _fixed_window,deque<yarp::sig::Vector> &_points, ImageOf<PixelRgb> *_imgIn, string _tag_file, double _threshold_median,
                                const Property &_filter_points_par, Vector &_x, Vector &_x_filtered, const Property &_filter_superq_par, const Property &_ipopt_par, const string &_homeContextPath, bool _save_points, ResourceFinder *_rf, superqTree *_superq_tree,
                                bool _merge_model, int _h_tree):
                                mutex_shared(_mutex_shared),filter_points(_filter_points), filter_superq(_filter_superq), single_superq(_single_superq),fixed_window( _fixed_window),tag_file(_tag_file),  threshold_median(_threshold_median), save_points(_save_points), imgIn(_imgIn),
                                filter_points_par(_filter_points_par),filter_superq_par(_filter_superq_par),ipopt_par(_ipopt_par), Thread(), homeContextPath(_homeContextPath), x(_x), x_filtered(_x_filtered), points(_points), rf(_rf), superq_tree(_superq_tree),
                                merge_model(_merge_model), h_tree(_h_tree)
{
}

/***********************************************************************/
void SuperqComputation::setPointsFilterPar(const Property &newOptions, bool first_time)
{
    LockGuard lg(mutex);

    double radiusValue=newOptions.find("filter_radius").asDouble();
    if (newOptions.find("filter_radius").isNull() && (first_time==true))
    {
        radius=0.01;
    }
    else if (!newOptions.find("filter_radius").isNull())
    {
        if ((radiusValue>0.0000001) && (radiusValue<0.01))
        {
            radius=radiusValue;
        }
        else if ((radiusValue>=0.01))
        {
            radius=0.01;
        }
        else if ((radiusValue<=0.0000001))
        {
            radius=0.0000001;
        }
    }

   int nnThreValue=newOptions.find("filter_nnThreshold").asInt();
    if (newOptions.find("filter_nnThreshold").isNull() && (first_time==true))
    {
        nnThreshold=100;
    }
    else if (!newOptions.find("filter_nnThreshold").isNull())
    {
        if ((nnThreValue>0) && (nnThreValue<100))
        {
                nnThreshold=nnThreValue;
        }
        else
        {
            nnThreshold=100;
        }
    }
}

/***********************************************************************/
Property SuperqComputation::getPointsFilterPar()
{
    Property advOptions;
    advOptions.put("filter_radius",radius);
    advOptions.put("filter_nnThreshold",nnThreshold);
    return advOptions;
}

/***********************************************************************/
void SuperqComputation::setSuperqFilterPar(const Property &newOptions, bool first_time)
{
    int mOrderValue=newOptions.find("median_order").asInt();
    if (newOptions.find("median_order").isNull() && (first_time==true))
    {
        std_median_order=3;
    }
    else if (!newOptions.find("median_order").isNull())
    {
        if((mOrderValue>=1) && (mOrderValue<=50))
        {
            std_median_order=mOrderValue;
        }
        else if (mOrderValue<1)
        {
            std_median_order=1;
        }
        else if (mOrderValue>50)
        {
            std_median_order=50;
        }
    }

    mOrderValue=newOptions.find("min_median_order").asInt();
    if (newOptions.find("min_median_order").isNull() && (first_time==true))
    {
        min_median_order=1;
    }
    else if (!newOptions.find("min_median_order").isNull())
    {
        if ((mOrderValue>=1) && (mOrderValue<=50))
        {
            min_median_order=mOrderValue;
        }
        else if (mOrderValue<1)
        {
            min_median_order=1;
        }
        else if (mOrderValue>50)
        {
            min_median_order=50;
        }
    }

    mOrderValue=newOptions.find("max_median_order").asInt();
    if (newOptions.find("max_median_order").isNull() && (first_time==true))
    {
        max_median_order=30;
    }
    else if (!newOptions.find("max_median_order").isNull())
    {
        if ((mOrderValue>=1) && (mOrderValue<=50) && (mOrderValue>=min_median_order))
        {
            max_median_order=mOrderValue;
        }
        else if ((mOrderValue<1) || (mOrderValue<min_median_order))
        {
            max_median_order=min_median_order;
        }
        else if (mOrderValue>50)
        {
            max_median_order=50;
        }
    }

    double threValue=newOptions.find("threshold_median").asDouble();
    if (newOptions.find("threhsold_median").isNull() && (first_time==true))
    {
        threshold_median=0.1;
    }
    else if (!newOptions.find("threhsold_median").isNull())
    {
        if ((threValue>0.005) && (threValue<=2.0))
        {
            threshold_median=threValue;
        }
        else if (threValue<0.005)
        {
            threshold_median=0.005;
        }
        else if (threValue>2.0)
        {
            threshold_median=2.0;
        }
    }

    double minNormVel=newOptions.find("min_norm_vel").asDouble();
    if (newOptions.find("min_norm_vel").isNull() && (first_time==true))
    {
        min_norm_vel=0.01;
    }
    else if (!newOptions.find("min_norm_vel").isNull())
    {
        if ((minNormVel>0.005) && (minNormVel<=0.1))
        {
            min_norm_vel=minNormVel;
        }
        else if (minNormVel<0.005)
        {
            min_norm_vel=0.005;
        }
        else if (minNormVel>0.1)
        {
            min_norm_vel=0.1;
        }
    }

    string par=newOptions.find("fixed_window").asString();
    if (newOptions.find("fixed_window").isNull() && (first_time==true))
    {
        fixed_window=false;
    }
    else if (!newOptions.find("fixed_window").isNull())
    {
        if ((par=="on") || (par=="off"))
        {
            fixed_window=(par=="on");
        }
        else
        {
            fixed_window=false;
        }
    }
}

/***********************************************************************/
Property SuperqComputation::getSuperqFilterPar()
{
    Property advOptions;
    if (fixed_window)
        advOptions.put("fixed_window","on");
    else
        advOptions.put("fixed_window","off");
    advOptions.put("median_order",std_median_order);
    advOptions.put("min_median_order",min_median_order);
    advOptions.put("max_median_order",max_median_order);
    advOptions.put("threshold_median",threshold_median);
    advOptions.put("min_norm_vel",min_norm_vel);
    return advOptions;
}

/***********************************************************************/
void SuperqComputation::setIpoptPar(const Property &newOptions, bool first_time)
{
    int points=newOptions.find("optimizer_points").asInt();
    if (newOptions.find("optimizer_points").isNull() && (first_time==true))
    {
        optimizer_points=50;
    }
    else if (!newOptions.find("optimizer_points").isNull())
    {
        if ((points>=10) && (points<=300))
        {
            optimizer_points=points;
        }
        else if (points<10)
        {
            optimizer_points=10;
        }
        else if (points>300)
        {
            optimizer_points=300;
        }
    }

    h_tree=newOptions.find("h_tree").asInt();
    if (newOptions.find("h_tree").isNull() && (first_time==true))
    {
        h_tree = 2;
        n_nodes=pow(2,h_tree+1) - 1;
    }
    else if (!newOptions.find("n_nodes").isNull())
    {
        if ((h_tree>=1))
        {         
            n_nodes=pow(2,h_tree+1) - 1;
        }
        else
        {
            h_tree = 2;
            n_nodes=pow(2,h_tree+1) - 1;
        }
    }

    double maxCpuTime=newOptions.find("max_cpu_time").asDouble();
    if (newOptions.find("max_cpu_time").isNull() && (first_time==true))
    {
        max_cpu_time=5.0;
    }
    else if (!newOptions.find("max_cpu_time").isNull())
    {
        if ((maxCpuTime>=0.01) && (maxCpuTime<=10.0))
        {
            max_cpu_time=maxCpuTime;
        }
        else if (maxCpuTime<0.01)
        {
            max_cpu_time=0.01;
        }
        else if (maxCpuTime>10.0)
        {
            max_cpu_time=10.0;
        }
    }

    double tolValue=newOptions.find("tol").asDouble();
    if (newOptions.find("tol").isNull() && (first_time==true))
    {
        tol=1e-5;
    }
    else if (!newOptions.find("tol").isNull())
    {
        if ((tolValue>1e-8) && (tolValue<=0.01))
        {
            tol=tolValue;
        }
        else if (tolValue<1e-8)
        {
            tol=1e-8;
        }
        else if (tolValue>0.01)
        {
            tol=0.01;
        }
    }

    int accIter=newOptions.find("acceptable_iter").asInt();
    if (newOptions.find("acceptable_iter").isNull() && (first_time==true))
    {
        acceptable_iter=0;
    }
    else if (!newOptions.find("acceptable_iter").isNull())
    {
        if ((accIter>=0 )&& (accIter<=10))
        {
             acceptable_iter=accIter;
        }
        else if (accIter<0)
        {
            acceptable_iter=0;
        }
        else if (accIter>10)
        {
            acceptable_iter=10;
        }
    }

    int maxIter=newOptions.find("max_iter").asInt();
    if (newOptions.find("max_iter").isNull() && (first_time==true))
    {
        max_iter=100;
    }
    else if (!newOptions.find("max_iter").isNull())
    {
        if ((maxIter>1))
        {
            max_iter=maxIter;
        }
        else
        {
            max_iter=100;
        }
    }

    string mu_str=newOptions.find("mu_strategy").asString();
    if (newOptions.find("mu_strategy").isNull() && (first_time==true))
    {
        mu_strategy="monotone";
    }
    else if (!newOptions.find("mu_strategy").isNull())
    {
        if ((mu_str=="adaptive") || (mu_str=="monotone"))
        {
            mu_strategy=mu_str;
        }
        else
        {
            mu_strategy="monotone";
        }
    }

    string nlp=newOptions.find("nlp_scaling_method").asString();
    if (newOptions.find("nlp_scaling_method").isNull() && (first_time==true))
    {
        nlp_scaling_method="gradient-based";
    }
    else if (!newOptions.find("nlp_scaling_method").isNull())
    {
        if ((nlp=="none") || (nlp=="gradient-based"))
        {
            nlp_scaling_method=nlp;
        }
        else
        {
            nlp_scaling_method="gradient-based";
        }
    }
}

/***********************************************************************/
Property SuperqComputation::getIpoptPar()
{
    LockGuard lg(mutex);

    Property advOptions;
    advOptions.put("optimizer_points",optimizer_points);
    advOptions.put("max_cpu_time",max_cpu_time);
    advOptions.put("tol",tol);
    advOptions.put("max_iter",max_iter);
    advOptions.put("acceptable_iter",acceptable_iter);
    advOptions.put("IPOPT_mu_strategy",mu_strategy);
    advOptions.put("IPOPT_nlp_scaling_method",nlp_scaling_method);
    return advOptions;
}

/***********************************************************************/
void SuperqComputation::setPar(const string &par_name, const string &value)
{
    if (par_name=="tag_file")
        tag_file=value;
    else if (par_name=="filter_points")
        filter_points=(value=="on");
    else if (par_name=="filter_superq")
        filter_superq=(value=="on");
    else if (par_name=="save_points")
        save_points=(value=="on");
    else if (par_name=="one_shot")
        one_shot=(value=="on");
    else if (par_name=="object_class")
        ob_class=value;
    else if (par_name=="single_superq")
        single_superq=(value=="on");
    else if (par_name=="debug")
        debug=(value=="on");
}

/***********************************************************************/
double SuperqComputation::getTime()
{
    return t_superq;
}

/***********************************************************************/
bool SuperqComputation::threadInit()
{
    yInfo()<<"[SuperqComputation]: Thread initing ... ";

    if (filter_points==true)
        setPointsFilterPar(filter_points_par, true);

    if (filter_superq==true)
        setSuperqFilterPar(filter_superq_par, true);

    setIpoptPar(ipopt_par, true);

    configFilterSuperq();
    config3Dpoints();

    bounds_automatic=true;
    one_shot=false;

    superq_computed=false;

    yDebug()<<"[SuperqComputation]: Resize of x";
    x.resize(11,0.00);
    x_filtered.resize(11,0.0);
    yDebug()<<"[SuperqComputation]: After resize of x";

    count_file=0;

    return true;
}

/***********************************************************************/
void SuperqComputation::run()
{
    while (!isStopping())
    {
        t0=Time::now();

        if (one_shot==false)
            getPoints3D();

        if (points.size()>0)
        {
            mutex.lock();

            if (single_superq)
            {
                if (points.size()>0)
                {
                    yInfo()<<"[SuperqComputation]: number of points acquired:"<< points.size();
                    go_on=computeSuperq();
                }
            }
            else if (superq_computed==false)
            {
                double t0_in, t_in;
                t0_in=Time::now();

                iterativeModeling();

                if (debug)
                    superq_tree->printTree(superq_tree->root);

                if (merge_model)
                {
                    double t_merge;
                    t_merge=Time::now();
                    go_on=superq_computed=findImportantPlanes(superq_tree->root);
                    superq_tree_new= new superqTree();

                    go_on=superq_computed=generateFinalTree(superq_tree->root, superq_tree_new->root);
                    superq_tree->root=superq_tree_new->root;

                    t_merge=Time::now() - t_merge;

                    yInfo()<<">>>>>>>>>>>>>> Computation time for merging model: "<<t_merge;
                }
                else
                    go_on=superq_computed=true;

                t_in=Time::now() - t0_in;

                yInfo()<<">>>>>>>>>>>>>> Computation time of multiple superquadrics model: "<<t_in;


                yDebug()<<"[SuperqComputation]: The superquadric has been computed "<<superq_computed;

            }

            if ((go_on==false) && (points.size()>0))
            {
                yError("[SuperqComputation]: Not found a suitable superquadric! ");
            }

            mutex.unlock();
        }
        else
        {
            x_filtered.resize(11,0.0);

            Time::delay(0.15);
        }

        t_superq=Time::now() - t0;
    }
}

/***********************************************************************/
void SuperqComputation::threadRelease()
{
    yInfo()<<"[SuperComputation]: Thread releasing ... ";

    if (!pointPort.isClosed())
        pointPort.close();

    if (mFilter!=NULL)
        delete mFilter;

    if (PolyEst!=NULL)
        delete PolyEst;

    if (superq_tree!=NULL)
        delete superq_tree;
}

/***********************************************************************/
bool SuperqComputation::configFilterSuperq()
{
    x.resize(11,0.0);
    new_median_order=1;

    std_median_order=5;
    max_median_order=30;

    mFilter = new MedianFilter(median_order, x);
    PolyEst =new AWLinEstimator(max_median_order, threshold_median);

    return true;
}

/***********************************************************************/
bool SuperqComputation::config3Dpoints()
{
    pointPort.open("/superquadric-model/point:i");

    return true;
}

/***********************************************************************/
void SuperqComputation::getPoints3D()
{
    Bottle *reply;

    reply=pointPort.read(false);

    if (reply!=NULL)
    {
        points.clear();

        if (Bottle *list=reply->get(0).asList())
        {
            for (int i=0; i<list->size();i++)
            {
                if (Bottle *pp=list->get(i).asList())
                {
                    Vector tmp(3,0.0);
                    tmp[0]=pp->get(0).asDouble();
                    tmp[1]=pp->get(1).asDouble();
                    tmp[2]=pp->get(2).asDouble();


                    points.push_back(tmp);
                }
                else
                {
                    yError()<<"[SuperqComputation]: Some problems in blob pixels!";
                }
            }
        }
        else
        {
            yError()<<"[SuperqComputation]: Some problem  in object blob!";
        }
    }
    else
    {
        yWarning("[SuperqComputation]: 3D points not received!");
    }
}

/***********************************************************************/
void SuperqComputation::savePoints(const string &namefile, const Vector &colors)
{
    ofstream fout;
    stringstream ss;
    ss << count_file;
    string count_file_str=ss.str();
    fout.open((homeContextPath+namefile+count_file_str+".off").c_str());

    if (fout.is_open())
    {
        fout<<"COFF"<<endl;
        fout<<points.size()<<" 0 0"<<endl;
        fout<<endl;
        for (size_t i=0; i<points.size(); i++)
        {
            int r=points[i][3];
            int g=points[i][4];
            int b=points[i][5];
            fout<<points[i].subVector(0,2).toString(3,3)<<" "<<r<<" "<<g<<" "<<b<<endl;
        }

        fout<<endl;
    }
    else
        yError()<<"[SuperqComputation]: Some problems in opening output file!";

    fout.close();

    count_file++;
}

/***********************************************************************/
bool SuperqComputation::readPointCloud()
{
    ifstream pointsFile(pointCloudFileName.c_str());
    points.clear();
    int nPoints;
    int state=0;
    string line;

    if (!pointsFile.is_open())
    {
        yError()<<"[SuperqComputation]: problem opening point cloud file!";
        return false;
    }

    while (!pointsFile.eof())
    {
        getline(pointsFile,line);
        Bottle b(line);
        Value firstItem=b.get(0);
        bool isNumber=firstItem.isInt() || firstItem.isDouble();

        if (state==0)
        {
            string tmp=firstItem.asString().c_str();
            std::transform(tmp.begin(),tmp.end(),tmp.begin(),::toupper);
            if (tmp=="OFF" || tmp=="COFF")
                state++;
        }
        else if (state==1)
        {
            if (isNumber)
            {
                nPoints=firstItem.asInt();
                state++;
            }
        }
        else if (state==2)
        {
            if (isNumber && (b.size()>=3))
            {
                Vector point(3,0.0);
                point[0]=b.get(0).asDouble();
                point[1]=b.get(1).asDouble();
                point[2]=b.get(2).asDouble();
                points.push_back(point);

                if (--nPoints<=0)
                    return true;
            }
        }
    }

    return false;
}

/***********************************************************************/
void SuperqComputation::filter()
{
    numVertices=points.size();

    cv:: Mat data(numVertices,3,CV_32F);

    for (int i=0; i<numVertices; i++)
    {
        Vector point=points[i];
        data.at<float>(i,0)=(float)point[0];
        data.at<float>(i,1)=(float)point[1];
        data.at<float>(i,2)=(float)point[2];
    }

    points.clear();

    yInfo()<<"[SuperqComputation]: Processing points...";
    double t0=yarp::os::Time::now();
    SpatialDensityFilter::filter(data,radius,nnThreshold+1, points);
    double t1=yarp::os::Time::now();
    yInfo()<<"[SuperqComputation]: Processed in "<<1e3*(t1-t0)<<" [ms]";

    Vector colors(3,0.0);
    colors[1]=255;

    savePoints("/filtered-"+tag_file, colors);
}

/***********************************************************************/
Vector SuperqComputation::computeOneShot(const deque<Vector> &p)
{
    yDebug()<<"[SuperqComputation]: Clearning points "<<points.size();

    points.clear();

    for (int i=0; i<p.size(); i++)
    {
        points.push_back(p[i]);
    }
    yDebug()<<"New points "<<points.size();

    yInfo()<<"[SuperqComputation]: Thread initing ... ";

    if (filter_points==true)
        setPointsFilterPar(filter_points_par, true);

    if (filter_superq==true)
        setSuperqFilterPar(filter_superq_par, true);

    setIpoptPar(ipopt_par, true);

    configFilterSuperq();
    config3Dpoints();

    bounds_automatic=true;
    one_shot=false;

    superq_computed=false;

    yDebug()<<"[SuperqComputation]: Resize of x";
    x.resize(11,0.00);
    x_filtered.resize(11,0.0);
    yDebug()<<"[SuperqComputation]: After resize of x";

    count_file=0;

    Vector colors(3,0.0);
    colors[1]=255;

    savePoints("/3Dpoints-"+tag_file, colors);

    Ipopt::SmartPtr<Ipopt::IpoptApplication> app=new Ipopt::IpoptApplication;
    app->Options()->SetNumericValue("tol",tol);
    app->Options()->SetIntegerValue("acceptable_iter",acceptable_iter);
    app->Options()->SetStringValue("mu_strategy",mu_strategy);
    app->Options()->SetIntegerValue("max_iter",max_iter);
    app->Options()->SetNumericValue("max_cpu_time",max_cpu_time);
    app->Options()->SetStringValue("nlp_scaling_method",nlp_scaling_method);
    app->Options()->SetStringValue("hessian_approximation","limited-memory");
    app->Options()->SetIntegerValue("print_level",0);
    app->Initialize();

    Ipopt::SmartPtr<SuperQuadric_NLP> superQ_nlp= new SuperQuadric_NLP;

    superQ_nlp->init();
    superQ_nlp->configure(this->rf,bounds_automatic, ob_class);

    superQ_nlp->setPoints(points, optimizer_points);

    double t0_superq=Time::now();

    yDebug()<<"[SuperqComputation]: Start IPOPT ";

    Ipopt::ApplicationReturnStatus status=app->OptimizeTNLP(GetRawPtr(superQ_nlp));

    yDebug()<<"[SuperqComputation]: Finish IPOPT ";

    double t_s=Time::now()-t0_superq;

    if (status==Ipopt::Solve_Succeeded)
    {
        x=superQ_nlp->get_result();
        yInfo("[SuperqComputation]: Solution of the optimization problem: %s", x.toString(3,3).c_str());
        yInfo("[SuperqComputation]: Execution time : %f", t_s);
        return x;
    }
    else if(status==Ipopt::Maximum_CpuTime_Exceeded)
    {
        x=superQ_nlp->get_result();
        yWarning("[SuperqComputation]: Solution after maximum time exceeded: %s", x.toString(3,3).c_str());
        return x;
    }
    else
    {
        x.resize(11,0.0);
        return x;
    }
}

/***********************************************************************/
void SuperqComputation::computeOneShotMultiple(const deque<Vector> &p)
{
    points.clear();

    for (int i=0; i<p.size(); i++)
    {
        points.push_back(p[i]);
    }

    yInfo()<<"[SuperqComputation]: Thread initing ... ";

    if (filter_points==true)
        setPointsFilterPar(filter_points_par, true);

    if (filter_superq==true)
        setSuperqFilterPar(filter_superq_par, true);

    setIpoptPar(ipopt_par, true);

    configFilterSuperq();
    config3Dpoints();

    bounds_automatic=true;
    one_shot=false;

    superq_computed=false;

    x.resize(11,0.00);
    x_filtered.resize(11,0.0);

    count_file=0;

    double t0_in, t_in;
    t0_in=Time::now();

    superq_tree->reset();

    iterativeModeling();

    if (debug)
        superq_tree->printTree(superq_tree->root);

    if (merge_model)
    {
        double t_merge;
        t_merge=Time::now();

        go_on=superq_computed=findImportantPlanes(superq_tree->root);

        superq_tree_new= new superqTree();

        go_on=superq_computed=generateFinalTree(superq_tree->root, superq_tree_new->root);

        superq_tree->root=superq_tree_new->root;

        //if (debug)
            superq_tree->printTree(superq_tree->root);

        t_merge=Time::now() - t_merge;

        yInfo()<<">>>>>>>>>>>>>> Computation time for merging model: "<<t_merge;
    }
    else
        go_on=superq_computed=true;

    t_in=Time::now() - t0_in;

    yInfo()<<"[SuperqComputation]: Computation time of multiple superquadrics model: "<<t_in;

    yDebug()<<"[SuperqComputation]: The superquadric has been computed: "<<superq_computed;

}

/***********************************************************************/
bool SuperqComputation::computeSuperq()
{
    Vector colors(3,0.0);
    colors[1]=255;

    savePoints("/3Dpoints-"+tag_file, colors);

    Ipopt::SmartPtr<Ipopt::IpoptApplication> app=new Ipopt::IpoptApplication;
    app->Options()->SetNumericValue("tol",tol);
    app->Options()->SetIntegerValue("acceptable_iter",acceptable_iter);
    app->Options()->SetStringValue("mu_strategy",mu_strategy);
    app->Options()->SetIntegerValue("max_iter",max_iter);
    app->Options()->SetNumericValue("max_cpu_time",max_cpu_time);
    app->Options()->SetStringValue("nlp_scaling_method",nlp_scaling_method);
    app->Options()->SetStringValue("hessian_approximation","limited-memory");
    app->Options()->SetIntegerValue("print_level",0);
    app->Initialize();

    Ipopt::SmartPtr<SuperQuadric_NLP> superQ_nlp= new SuperQuadric_NLP;

    superQ_nlp->init();
    superQ_nlp->configure(this->rf,bounds_automatic, ob_class);

    superQ_nlp->setPoints(points, optimizer_points);

    double t0_superq=Time::now();

    yDebug()<<"[SuperqComputation]: Start IPOPT ";

    Ipopt::ApplicationReturnStatus status=app->OptimizeTNLP(GetRawPtr(superQ_nlp));

    yDebug()<<"[SuperqComputation]: Finish IPOPT ";

    double t_s=Time::now()-t0_superq;

    if (status==Ipopt::Solve_Succeeded)
    {
        x=superQ_nlp->get_result();
        yInfo("[SuperqComputation]: Solution of the optimization problem: %s", x.toString(3,3).c_str());
        yInfo("[SuperqComputation]: Execution time : %f", t_s);
        return true;
    }
    else if(status==Ipopt::Maximum_CpuTime_Exceeded)
    {
        x=superQ_nlp->get_result();
        yWarning("[SuperqComputation]: Solution after maximum time exceeded: %s", x.toString(3,3).c_str());
        return true;
    }
    else
    {
        x.resize(11,0.0);
        return false;
    }
}

/***********************************************************************/
Vector SuperqComputation::computeMultipleSuperq(const deque<Vector> &points_splitted)
{
    Vector colors(3,0.0);
    colors[1]=255;

    Ipopt::SmartPtr<Ipopt::IpoptApplication> app=new Ipopt::IpoptApplication;
    app->Options()->SetNumericValue("tol",tol);
    app->Options()->SetIntegerValue("acceptable_iter",acceptable_iter);
    app->Options()->SetStringValue("mu_strategy",mu_strategy);
    app->Options()->SetIntegerValue("max_iter",max_iter);
    app->Options()->SetNumericValue("max_cpu_time",max_cpu_time);
    app->Options()->SetStringValue("nlp_scaling_method",nlp_scaling_method);
    app->Options()->SetStringValue("hessian_approximation","limited-memory");
    app->Options()->SetIntegerValue("print_level",0);
    app->Initialize();

    Ipopt::SmartPtr<SuperQuadric_NLP> superQ_nlp= new SuperQuadric_NLP;

    superQ_nlp->init();

    superQ_nlp->configure(this->rf,bounds_automatic, ob_class);

    superQ_nlp->setPoints(points_splitted, optimizer_points);

    double t0_superq=Time::now();

    yDebug()<<"[SuperqComputation]: Start IPOPT ";

    Ipopt::ApplicationReturnStatus status=app->OptimizeTNLP(GetRawPtr(superQ_nlp));

    yDebug()<<"[SuperqComputation]: Finish IPOPT ";

    double t_s=Time::now()-t0_superq;

    if (status==Ipopt::Solve_Succeeded)
    {
        x=superQ_nlp->get_result();
        yInfo("[SuperqComputation]: Solution of the optimization problem: %s", x.toString(3,3).c_str());
        yInfo("[SuperqComputation]: Execution time : %f", t_s);
        return x;
    }
    else if(status==Ipopt::Maximum_CpuTime_Exceeded)
    {
        x=superQ_nlp->get_result();
        yWarning("[SuperqComputation]: Solution after maximum time exceeded: %s", x.toString(3,3).c_str());
        return x;
    }
    else
    {
        x.resize(11,0.0);
        return x;
    }
}

/***********************************************************************/
void SuperqComputation::filterSuperq()
{
    yInfo()<< "[SuperqComputation]: Filtering the last "<< median_order << " superquadrics...";

    yInfo()<< "[SuperqComputation]: x "<<x.toString();

    if (fixed_window)
    {
        if (median_order != std_median_order)
        {
            median_order=std_median_order;
            mFilter->setOrder(median_order);
        }
        x_filtered=mFilter->filt(x);
    }
    else
    {
        int new_median_order=adaptWindComputation();

        if (median_order != new_median_order)
        {
            median_order=new_median_order;
            mFilter->setOrder(median_order);
            x_filtered=mFilter->filt(x);
        }
        else
            x_filtered=mFilter->filt(x);
    }

    if (norm(x_filtered)==0.0)
        x_filtered=x;

    yInfo()<< "[SuperqComputation]: Filtered superq "<< x_filtered.toString(3,3);
}

/***********************************************************************/
void SuperqComputation::resetMedianFilter()
{
    x.resize(11,0.0);
    x_filtered.resize(11,0.0);

    mFilter->init(x);
}

/***********************************************************************/
int SuperqComputation::adaptWindComputation()
{
    elem_x.resize(3,0.0);
    elem_x=x.subVector(5,7);
    yInfo()<<"[SuperqComputation]: Old median order "<<median_order;

    AWPolyElement el(elem_x,Time::now());
    Vector vel=PolyEst->estimate(el);
    yInfo()<<"[SuperqComputation]: Velocity estimate "<<PolyEst->estimate(el).toString();


    if (norm(vel)>=min_norm_vel)
        new_median_order=min_median_order;
    else
    {
        if (new_median_order<max_median_order)
            new_median_order++;
    }

    yInfo()<<"[SuperqComputation]: New median order "<<new_median_order;
    return new_median_order;
}

/***********************************************************************/
Vector SuperqComputation::getSolution(bool filtered_superq)
{
    LockGuard lg(mutex);

    if (filtered_superq==false)
        return x;
    else
        return x_filtered;
}

/***********************************************************************/
void SuperqComputation::sendPoints(const deque<Vector> &p)
{
    LockGuard lg(mutex);

    yDebug()<<"[SuperqComputation]: Clearning points "<<points.size();

    points.clear();

    for (int i=0; i<p.size(); i++)
    {
        points.push_back(p[i]);
    }
    yDebug()<<"New points "<<points.size();
}

/***********************************************************************/
void SuperqComputation::iterativeModeling()
{
    setIpoptPar(ipopt_par, true);

    superq_tree->setPoints(&points);

    computeNestedSuperq(superq_tree->root);
}

/***********************************************************************/
void SuperqComputation::computeNestedSuperq(node *newnode)
{
    if ((newnode!=NULL))
    {
        cout<<endl;
        Vector superq1(11,0.0);
        Vector superq2(11,0.0);

        nodeContent node_c1;
        nodeContent node_c2;

        if ((newnode->height <= h_tree))
        {
            splitPoints(newnode);


            superq1=computeMultipleSuperq(points_splitted1);
            superq2=computeMultipleSuperq(points_splitted2);

            node_c1.superq=superq1;
            node_c2.superq=superq2;
            node_c1.point_cloud= new deque<Vector>;
            node_c2.point_cloud= new deque<Vector>;

            *node_c1.point_cloud=points_splitted1;
            *node_c2.point_cloud=points_splitted2;

            node_c1.height=newnode->height + 1;
            node_c2.height=newnode->height + 1;

            superq_tree->insert(node_c1, node_c2, newnode);
        }

        computeNestedSuperq(newnode->left);
        computeNestedSuperq(newnode->right);
    }
}

/***********************************************************************/
void SuperqComputation::splitPoints(node *leaf)
{
    deque<Vector> points_splitted;

    points_splitted=*leaf->point_cloud;

    points_splitted1.clear();
    points_splitted2.clear();

    Vector center(3,0.0);

    for (size_t k=0; k<points_splitted.size();k++)
    {
        Vector &point=points_splitted[k];
        center[0]+=point[0];
        center[1]+=point[1];
        center[2]+=point[2];
    }

    center[0]/=points_splitted.size();
    center[1]/=points_splitted.size();
    center[2]/=points_splitted.size();

    Matrix M=zeros(3,3);
    Matrix u(3,3);
    Matrix v(3,3);

    Vector s(3,0.0);
    Vector n(3,0.0);
    Vector o(3,0.0);
    Vector a(3,0.0);

    for (size_t i=0;i<points_splitted.size(); i++)
    {
        Vector &point=points_splitted[i];
        M(0,0)= M(0,0) + (point[1]-center[1])*(point[1]-center[1]) + (point[2]-center[2])*(point[2]-center[2]);
        M(0,1)= M(0,1) - (point[1]-center[1])*(point[0]-center[0]);
        M(0,2)= M(0,2) - (point[2]-center[2])*(point[0]-center[0]);
        M(1,1)= M(1,1) + (point[0]-center[0])*(point[0]-center[0]) + (point[2]-center[2])*(point[2]-center[2]);
        M(2,2)= M(2,2) + (point[1]-center[1])*(point[1]-center[1]) + (point[0]-center[0])*(point[0]-center[0]);
        M(1,2)= M(1,2) - (point[2]-center[2])*(point[1]-center[1]);
    }

    M(0,0)= M(0,0)/points_splitted.size();
    M(0,1)= M(0,1)/points_splitted.size();
    M(0,2)= M(0,2)/points_splitted.size();
    M(1,1)= M(1,1)/points_splitted.size();
    M(2,2)= M(2,2)/points_splitted.size();
    M(1,2)= M(1,2)/points_splitted.size();

    M(1,0)= M(0,1);
    M(2,0)= M(0,2);
    M(2,1)= M(1,2);

    SVDJacobi(M,u,s,v);
    n=u.getCol(2);

    Vector plane(4,0.0);

    plane[0]=n[0];
    plane[1]=n[1];
    plane[2]=n[2];
    plane[3]=(plane[0]*center[0]+plane[1]*center[1]+plane[2]*center[2]);

    leaf->plane=plane;

    for (size_t j=0; j<points_splitted.size(); j++)
    {
        Vector point=points_splitted[j];
        if (plane[0]*point[0]+plane[1]*point[1]+plane[2]*point[2]- plane[3] > 0)
            points_splitted1.push_back(point);
        else
            points_splitted2.push_back(point);
    }

    if (debug)
    {
        yDebug()<<"[SuperqComputation]: points_splitted 1 "<<points_splitted1.size();
        yDebug()<<"[SuperqComputation]: points_splitted 2 "<<points_splitted2.size();
    }
}

/****************************************************************/
bool SuperqComputation::findImportantPlanes(node *current_node)
{
    Matrix relations(3,3);

    superq_tree->root->plane_important=false;
    if (current_node->height < h_tree)
    {
        cout<<endl;
        yDebug()<<"|| Find important plane LEFT ";
        if (current_node->left !=NULL)
            findImportantPlanes(current_node->left);
         cout<<endl;
        yDebug()<<"|| Find important plane RIGHT ";
        if (current_node->right !=NULL)
            findImportantPlanes(current_node->right);
    }

    // if nothing is changed plane important for root is always true
    if (current_node->height > 1 && current_node->plane_important==false)
    {
        computeSuperqAxis(current_node->left);
        computeSuperqAxis(current_node->right);

        if (debug)
            yDebug()<<"node->height "<<current_node->height;


        if (axisParallel(current_node->left, current_node->right, relations) && sectionEqual(current_node->left, current_node->right, relations))
        {
            if (debug)
                yDebug()<<"|| To be merged, no plane important ";
            cout<<endl;

            current_node->plane_important=false;

            /*if (!superq_tree->searchPlaneImportant(current_node->left))
                current_node->left=NULL;
            if (!superq_tree->searchPlaneImportant(current_node->right))
                current_node->right=NULL;*/

            if (!superq_tree->searchPlaneImportant(current_node->left) && !superq_tree->searchPlaneImportant(current_node->right))
            {
                current_node->left=NULL;
                current_node->right=NULL;

                if (debug)
                    yDebug()<<"leaves set equal to NULL";
            }

        }
        else
        {
            yDebug()<<"Plane current node importat";
            cout<<endl;
            current_node->plane_important=true;

            node *node_uncle=((current_node==current_node->father->right)?current_node->father->left:current_node->father->right);

            if (node_uncle!=NULL)
            {
                computeSuperqAxis(node_uncle);

                double distance_right = edgesClose(current_node->right, node_uncle);
                double distance_left = edgesClose(current_node->left, node_uncle);

                if (debug)
                {
                    yDebug()<<"distance right "<<distance_right;
                    yDebug()<<"distance left "<<distance_left;
                }


                // Check if tolerance is needed
                if(distance_right < distance_left)
                    current_node->right->uncle_close=node_uncle;
                else
                    current_node->left->uncle_close=node_uncle;


                // Instead of checking if all the nephews are parallel to the uncle, check only the close one
                bool parallel_to_uncle;

                if (current_node->left->uncle_close!=NULL)
                {

                    parallel_to_uncle=(axisParallel(current_node->left, node_uncle, relations) && sectionEqual(current_node->left, node_uncle, relations));
                    if (parallel_to_uncle && debug)
                        yDebug()<<"Left is parallel and with same dimensions of its uncle";

                }
                else if (current_node->right->uncle_close!=NULL)
                {

                    parallel_to_uncle=(axisParallel(current_node->right, node_uncle, relations) && sectionEqual(current_node->right, node_uncle, relations));
                    if (parallel_to_uncle && debug)
                        yDebug()<<"Right is parallel and with same dimensions of its uncle";
                }

                yDebug()<<" || Parallel to uncle "<<parallel_to_uncle;

                if(parallel_to_uncle==false)
                {
                    yDebug()<<"|| Plane  father important ";
                    current_node->father->plane_important=true;
                }
                else
                {
                    current_node->father->plane_important=false;
                }
            }

        }

    }

    if (current_node->height==1)
    {
        computeSuperqAxis(current_node->left);
        computeSuperqAxis(current_node->right);

        yDebug()<<"|| Plane of root is important:  "<<current_node->plane_important;

        if ( axisParallel(current_node->left, current_node->right, relations) && !sectionEqual(current_node->left, current_node->right, relations))
            //if (!(axisParallel(current_node->left, current_node->right, relations) && !sectionEqual(current_node->left, current_node->right, relations)))
            //{
        {
             yDebug()<<__LINE__;
            current_node->plane_important=true;
        }


        if ((superq_tree->searchPlaneImportant(current_node->left)==false
                && superq_tree->searchPlaneImportant(current_node->right)==false))
            current_node->plane_important=true;
        //else
        //    current_node->plane_important=false;

        //if (current_node->left->plane_important==true
          //      && current_node->right->plane_important==true)
            //current_node->plane_important=true;

        yDebug()<<"|| Plane of root is important:  "<<current_node->plane_important;
    }

    return true;
}

/***********************************************************************/
void SuperqComputation::copySuperqChildren(node *old_node, node *newnode)
{
    nodeContent node_c1;
    nodeContent node_c2;

    node_c1.superq=old_node->left->superq;
    node_c2.superq=old_node->right->superq;

    node_c1.point_cloud= new deque<Vector>;
    node_c2.point_cloud= new deque<Vector>;


    node_c1.point_cloud=old_node->left->point_cloud;
    node_c2.point_cloud=old_node->right->point_cloud;

    node_c1.height=newnode->height + 1;
    node_c2.height=newnode->height + 1;

    // aCTUALLY THEY ARE NOT NEEDED
    node_c1.plane_important=old_node->left->plane_important;
    node_c2.plane_important=old_node->right->plane_important;

    superq_tree_new->insert(node_c2, node_c1, newnode);
}

/***********************************************************************/
bool SuperqComputation::generateFinalTree(node *old_node, node *newnode)
{
    if (old_node!=NULL && old_node->height <= h_tree)
    {
        cout<<endl;
        yDebug()<<"node height in merging"<<old_node->height;

        if (old_node->height > 1)
        {
            yDebug()<<"|| old_node->plane_important"<<old_node->plane_important;
            yDebug()<<"|| old_node->plane_important father"<<old_node->father->plane_important;
            if (old_node->plane_important==true && old_node->father->plane_important==false)
            {
                yDebug()<<"|| Current plane is important";
                superqUsingPlane(old_node,old_node->father->point_cloud, old_node->plane, newnode);

                generateFinalTree(old_node->left, newnode->left);
                generateFinalTree(old_node->right, newnode->right);


                //if (newnode->right->superq==old_node->right->superq || newnode->right->superq==old_node->left->superq)
                //{
                    //generateFinalTree(old_node->left, newnode->left);
                    //generateFinalTree(old_node->right, newnode->right);
                //}
                //else if (newnode->left->superq==old_node->right->superq || newnode->left->superq==old_node->left->superq)
                //{
                 //   generateFinalTree(old_node->left, newnode->left);
                 //   generateFinalTree(old_node->right, newnode->left);
                //}

            }
            else if (old_node->plane_important==true && old_node->father->plane_important==true)
            {
                yDebug()<<"|| Current and father's plane are important";

                // Since here I would compute again the superqs using the point cloud and the plane of the node
                // I can just copy the superq -> faster
                copySuperqChildren(old_node, newnode);


                generateFinalTree(old_node->left, newnode->left);
                generateFinalTree(old_node->right, newnode->right);
            }
            else if (old_node->left!=NULL && old_node->right!=NULL)
            {
                yDebug()<<__LINE__;

                //if (old_node->left->plane_important==true && old_node->right->plane_important==true)
                if (superq_tree->searchPlaneImportant(old_node->left)==true && superq_tree->searchPlaneImportant(old_node->right)==true)
                {
                    copySuperqChildren(old_node, newnode);

                    // Copy to have structure but not to save superq
                    //newnode->left->superq.zero();
                    //newnode->right->superq.zero();

                    generateFinalTree(old_node->left, newnode->left);
                    generateFinalTree(old_node->right, newnode->right);
                }
                else
                {
                    //if (old_node->left->plane_important==true)
                    if (superq_tree->searchPlaneImportant(old_node->left))
                    {
                        yDebug()<<"only left important";
                        generateFinalTree(old_node->left, newnode);
                    }
                    //else if (old_node->right->plane_important==true)
                    else if (superq_tree->searchPlaneImportant(old_node->right))
                    {
                        yDebug()<<"only right important";
                        generateFinalTree(old_node->right, newnode);
                    }
                }
            }

        }
        else
        {
            if (old_node->plane_important==true)
            {
                copySuperqChildren(old_node, newnode);
                generateFinalTree(old_node->left, newnode->left);
                generateFinalTree(old_node->right, newnode->right);
            }
            //else if (old_node->left->plane_important==true && old_node->right->plane_important==true)
            else if (superq_tree->searchPlaneImportant(old_node->left)==true && superq_tree->searchPlaneImportant(old_node->right)==true)
            {
                copySuperqChildren(old_node, newnode);

                // Copy to have structure but not to save superq
                //newnode->left->superq.zero();
                //newnode->right->superq.zero();

                generateFinalTree(old_node->left, newnode->left);
                generateFinalTree(old_node->right, newnode->right);
            }
            else
            {
                if (superq_tree->searchPlaneImportant(old_node->left)==true)
                {
                    yDebug()<<"only left ";
                    generateFinalTree(old_node->left, newnode);
                }
                if (superq_tree->searchPlaneImportant(old_node->right)==true)
                {
                    yDebug()<<"only right ";
                    generateFinalTree(old_node->right, newnode);
                }
            }

        }
    }

    return true;

}

/****************************************************************/
void SuperqComputation::superqUsingPlane(node *old_node, deque<Vector> *points, Vector &plane, node *newnode)
{
    points_splitted1.clear();
    points_splitted2.clear();

    Vector superq1, superq2;

    deque<Vector> points_splitted=*points;

    for (size_t j=0; j<points_splitted.size(); j++)
    {
        Vector point=points_splitted[j];
        if (plane[0]*point[0]+plane[1]*point[1]+plane[2]*point[2]- plane[3] > 0)
            points_splitted1.push_back(point);
        else
            points_splitted2.push_back(point);
    }

    if (debug)
    {
        yDebug()<<"[SuperqComputation]: New points_splitted 1 "<<points_splitted1.size();
        yDebug()<<"[SuperqComputation]: New points_splitted 2 "<<points_splitted2.size();
    }

    superq1=computeMultipleSuperq(points_splitted1);
    superq2=computeMultipleSuperq(points_splitted2);

    nodeContent node_c1;
    nodeContent node_c2;

    node_c1.superq=superq1;
    node_c2.superq=superq2;
    node_c1.point_cloud= new deque<Vector>;
    node_c2.point_cloud= new deque<Vector>;

    *node_c1.point_cloud=points_splitted1;
    *node_c2.point_cloud=points_splitted2;

    node_c1.height=newnode->height + 1;
    node_c2.height=newnode->height + 1;

    superq_tree_new->insert(node_c1, node_c2, newnode);

}

/****************************************************************/
void SuperqComputation::computeSuperqAxis(node *node)
{
    Matrix R=euler2dcm(node->superq.subVector(8,10));
    //node->R=R;
    node->axis_x = R.getCol(0).subVector(0,2);
    node->axis_y = R.getCol(1).subVector(0,2);
    node->axis_z = R.getCol(2).subVector(0,2);

    //yDebug()<<"              Axis "<<R.toString();
}

/****************************************************************/
bool SuperqComputation::axisParallel(node *node1, node *node2, Matrix &relations)
{
    // Noise
    //double threshold=0.7;
    // No Noisy
    double threshold=0.7;

    if (abs(dot(node1->axis_x, node2->axis_x)) > threshold)
    {
        relations(0,0) = 1;
    }
    //else if  (abs(dot(node1->axis_x, node2->axis_y)) > threshold)
    if  (abs(dot(node1->axis_x, node2->axis_y)) > threshold)
    {
        relations(0,1) = 1;
    }
    if  (abs(dot(node1->axis_x, node2->axis_z)) > threshold)
    {
        relations(0,2) = 1;
    }
    if (abs(dot(node1->axis_y, node2->axis_x)) > threshold)
    {
        relations(1,0) = 1;
    }
    if  (abs(dot(node1->axis_y, node2->axis_y)) > threshold)
    {
        relations(1,1) = 1;
    }
    if  (abs(dot(node1->axis_y, node2->axis_z))> threshold)
    {
        relations(1,2) = 1;
    }
    if (abs(dot(node1->axis_z, node2->axis_x)) > threshold)
    {
        relations(2,0) = 1;
    }
    if  (abs(dot(node1->axis_z, node2->axis_y)) > threshold)
    {
        relations(2,1) = 1;
    }
    if  (abs(dot(node1->axis_z, node2->axis_z)) > threshold)
    {
        relations(2,2) = 1;
    }

    //if (debug)
       // yDebug()<<"rel "<<relations.toString();

    if (norm(relations.getRow(0))> 1.0)
    {
        int i_max=0;
        double max=-1.0;
        if(abs(dot(node1->axis_x, node2->axis_x)) < abs(dot(node1->axis_x, node2->axis_y)))
        {
            max=abs(dot(node1->axis_x, node2->axis_y));
            i_max=1;
        }
        else
            max=abs(dot(node1->axis_x, node2->axis_x));

         if (max<abs(dot(node1->axis_x, node2->axis_z)))
         {
            max=abs(dot(node1->axis_x, node2->axis_z));
            i_max=2;
         }

         relations(0,0)=relations(0,1)=relations(0,2)=0.0;
         relations(0,i_max)=1.0;
    }

    if (norm(relations.getRow(1))> 1.0)
    {
        int i_max=0;
        double max=-1.0;
        if(abs(dot(node1->axis_y, node2->axis_x)) < abs(dot(node1->axis_y, node2->axis_y)))
        {
            max=abs(dot(node1->axis_y, node2->axis_y));
            i_max=1;
        }
        else
            max=abs(dot(node1->axis_y, node2->axis_x));

         if (max<abs(dot(node1->axis_y, node2->axis_z)))
         {
            max=abs(dot(node1->axis_y, node2->axis_z));
            i_max=2;
         }

         relations(1,0)=relations(1,1)=relations(1,2)=0.0;
         relations(1,i_max)=1.0;
    }

    if (norm(relations.getRow(2))> 1.0)
    {
        int i_max=0;
        double max=-1.0;
        if(abs(dot(node1->axis_z, node2->axis_x)) < abs(dot(node1->axis_z, node2->axis_y)))
        {
            max=abs(dot(node1->axis_z, node2->axis_y));
            i_max=1;
        }
        else
            max=abs(dot(node1->axis_z, node2->axis_x));

         if (max<abs(dot(node1->axis_z, node2->axis_z)))
         {
            max=abs(dot(node1->axis_z, node2->axis_z));
            i_max=2;
         }

         relations(2,0)=relations(2,1)=relations(2,2)=0.0;
         relations(2,i_max)=1.0;
    }

    yDebug()<<"rel "<<relations.toString();

    if ((norm(relations.getRow(0)) > 0.0) || (norm(relations.getRow(1)) > 0.0) || (norm(relations.getRow(2)) > 0.0))
    {
        yDebug()<<"axis parallel true";
        return true;
    }
    else
    {
        yDebug()<<"axis parallel false";
        return false;
    }

}

/****************************************************************/
/*bool SuperqComputation::sectionEqual(node *node1, node *node2, Matrix &relations)
{
    double threshold1=0.85;
    double threshold2=0.015;

    int parall1, parall2;

    Matrix R1(3,3);
    R1.setRow(0,node1->axis_x);
    R1.setRow(1,node1->axis_y);
    R1.setRow(2,node1->axis_z);

    Matrix R2(3,3);
    R2.setRow(0,node2->axis_x);
    R2.setRow(1,node2->axis_y);
    R2.setRow(2,node2->axis_z);


    if (norm(R2.getCol(0))>1 || norm(R2.getCol(1))>1 || norm(R2.getCol(2))>1)
        yError()<< "Something wrong in one column!!";

    for (size_t i=0; i<3; i++)
    {
        for (size_t j=0; j<3; j++)
        {
            if (relations(i,j)==1)
            {
                parall1=i;
                parall2=j;
            }
        }
    }

    Vector dim1=node1->superq.subVector(0,2);
    Vector dim2=node2->superq.subVector(0,2);

    yDebug()<<"axis "<<parall1<< "is parallel to axis "<<parall2;
    yDebug()<<R1.getRow(parall1).toString()<<" // to "<<R2.getRow(parall2).toString();

    bool equal;
    //if(debug)
    //{
        yDebug()<<"||            Dim 1     "<<dim1.toString();
        yDebug()<<"||            Dim 2     "<<dim2.toString();
   // }


    Vector p1,p2,p3,p4;
    p1.resize(3,0.0);
    p2.resize(3,0.0);
    p3.resize(3,0.0);
    p4.resize(3,0.0);

    p1=node1->superq.subVector(5,7)+dim1[parall1]*R1.getRow(parall1);
    p2=node1->superq.subVector(5,7)-dim1[parall1]*R1.getRow(parall1);


    p3=node2->superq.subVector(5,7)+dim2[parall2]*R2.getRow(parall2);
    p4=node2->superq.subVector(5,7)-dim2[parall2]*R2.getRow(parall2);

    double cos_max_dist1, cos_max_dist2;
    cos_max_dist1=dot((p1 - p2)/norm(p1 - p2), (p1 - p4)/norm(p1 - p4));
    cos_max_dist2=dot((p3 - p4)/norm(p3 - p4), (p1 - p4)/norm(p1 - p4));

    yDebug()<<"cos_max_dist1"<<cos_max_dist1;
    yDebug()<<"cos_max_dist2"<<cos_max_dist2;

    if (abs(max(cos_max_dist1, cos_max_dist2)) > threshold1)
    {
        //equal=true;
        int count_true=0;

        for (size_t j=0; j<3; j++)
        {
            for (size_t k=0; k<3; k++)
            {

                if (k != parall2 && j!=parall1)
                {
                    yDebug()<<"Comparing "<<dim1[j]<<" with "<<dim2[k];
                    if ( (abs(dim1[j]-dim2[k]) < threshold2))
                    {
                        //equal=equal && true;
                        yDebug()<<"ok";
                        count_true++;
                    }

                }
            }

        }
        yDebug()<<"count_true "<<count_true;
        if (count_true < 2)
            equal=false;
        else
            equal=true;

    }
    else
    {
        Vector distance_centers=node1->superq.subVector(5,7) - node2->superq.subVector(5,7);

        yDebug()<<"distance centers "<<distance_centers.toString();

        yDebug()<<"dot "<<dot((p1 - p2)/norm(p1 - p2), distance_centers/norm(distance_centers));

        //if (abs(dot((p1 - p2)/norm(p1 - p2), distance_centers/norm(distance_centers))) < 0.2)
        //{
            yDebug()<<"Comparing "<<dim1[parall1]<<" with "<<dim2[parall2];

            if ((abs(dim1[parall1]- dim2[parall2]) < threshold2))
            {
                yDebug()<<"ok";
                equal=true;
            }
            else
                equal=false;
        //}
       // else
       //     equal=false;

    }

    yDebug()<<"returning equal "<<equal;
    return equal;
}*/

/****************************************************************/
bool SuperqComputation::sectionEqual(node *node1, node *node2, Matrix &relations)
{
    double threshold1=0.6;
    //double threshold2=2.0;

    double threshold2=0.015;

    Matrix R1(3,3);
    R1.setRow(0,node1->axis_x);
    R1.setRow(1,node1->axis_y);
    R1.setRow(2,node1->axis_z);

    Matrix R2(3,3);
    R2.setRow(0,node2->axis_x);
    R2.setRow(1,node2->axis_y);
    R2.setRow(2,node2->axis_z);


    if (norm(R2.getCol(0))>1 || norm(R2.getCol(1))>1 || norm(R2.getCol(2))>1)
        yError()<< "Something wrong in one column!!";

    Matrix R2_rot(3,3);
    R2_rot=relations*R2;

    Vector dim1=node1->superq.subVector(0,2);
    Vector dim2=node2->superq.subVector(0,2);

    Vector dim2_rot=relations*dim2;


    //if(debug)
    //{
        yDebug()<<"||            Dim 1     "<<dim1.toString();
        yDebug()<<"||            Dim 2     "<<dim2.toString();
        yDebug()<<"||            Dim 2 rot "<<dim2_rot.toString();
    //}


    Vector p1,p2,p3,p4;
    p1.resize(3,0.0);
    p2.resize(3,0.0);
    p3.resize(3,0.0);
    p4.resize(3,0.0);

    deque<bool> equals;

    equals.clear();

     for (size_t i=0; i<3; i++)
    {
        bool equal;
        int other_index;

        if (norm(relations.getRow(i))> 0.0)
        {
            other_index=i;

            p1=node1->superq.subVector(5,7)+dim1[i]*R1.getRow(i);
            p2=node1->superq.subVector(5,7)-dim1[i]*R1.getRow(i);


            p3=node2->superq.subVector(5,7)+dim2_rot[other_index]*R2_rot.getRow(i);
            p4=node2->superq.subVector(5,7)-dim2_rot[other_index]*R2_rot.getRow(i);

            vector<double> distances;
            deque<Vector> vectors;
            vectors.push_back(p1 - p3);
            vectors.push_back(p1 - p4);
            vectors.push_back(p2 - p3);
            vectors.push_back(p2 - p4);
            distances.push_back(norm(vectors[0]));
            distances.push_back(norm(vectors[1]));
            distances.push_back(norm(vectors[2]));
            distances.push_back(norm(vectors[3]));

            auto it=max_element(distances.begin(), distances.end());

            Vector max_dist;
            max_dist=vectors[it -distances.begin()];

            double cos_max_dist1, cos_max_dist2;
            cos_max_dist1=dot((p1 - p2)/norm(p1 - p2), (p1 - p4)/norm(p1 - p4));
            cos_max_dist2=dot((p3 - p4)/norm(p3 - p4), (p1 - p4)/norm(p1 - p4));

            yDebug()<<"cos_max_dist1"<<cos_max_dist1;
            yDebug()<<"cos_max_dist2"<<cos_max_dist2;

            if (abs(max(cos_max_dist1, cos_max_dist2)) > threshold1)
            {
                equal=true;

                for (size_t j=0; j<3; j++)
                {
                    if ( i != j && dim2_rot[j]!= 0.0)
                    {

                        yDebug()<<"dim1[j]/dim2_rot[j] "<< dim1[j]/dim2_rot[j] <<"dim2[j]/dim1[j] "<<dim2_rot[j]/dim1[j];
                        yDebug()<<"threshold "<<1*threshold2<< " "<<1/threshold2;

                        //if ( (dim1[j] > dim2_rot[j] && (dim1[j]/dim2_rot[j] < 1*threshold2)) || (dim1[j] < dim2_rot[j]  && dim1[j]/dim2_rot[j] > 1/threshold2))
                        if (abs(dim1[j] - dim2_rot[j])< threshold2)
                        {
                            equal=equal && true;
                        }
                        else
                            equal=equal && false;
                    }
                }


            }
            else
            {
                if ( dim2_rot[i]!= 0.0)
                {
                    yDebug()<<"dim1[i]/dim2_rot[i] "<< dim1[i]/dim2_rot[i] <<"dim2[i]/dim1[i] "<<dim2_rot[i]/dim1[i];
                    yDebug()<<"threshold "<<1*threshold2<< " "<<1/threshold2;
                    //if ( (dim1[i] > dim2_rot[i] && (dim1[i]/dim2_rot[i] < 1*threshold2)) || (dim1[i] < dim2_rot[i]  && dim1[i]/dim2_rot[i] > 1/threshold2))
                    if (abs(dim1[i] - dim2_rot[i])< threshold2)
                    {
                        equal=true;
                    }
                    else
                        equal=false;
                }
            }
            equals.push_back(equal);
        }
        equals.push_back(true);

    }

    return equals[0] && equals[1] && equals[2];
}


/****************************************************************/
/*bool SuperqComputation::sectionEqual(node *node1, node *node2, Matrix &relations)
{
    // No noise
    double threshold=0.02;
    // Noise
    //double threshold=0.03;


    Vector dim1=node1->superq.subVector(0,2);
    Vector dim2=node2->superq.subVector(0,2);

    Vector dim_rot=relations*dim2;

    if(debug)
    {
        yDebug()<<"     Dim 1 "<<dim1.toString();
        yDebug()<<"     Dim 2 rot "<<dim_rot.toString();
    }


    bool equal=true;

    for (size_t i=0; i<3; i++)
    {
        if (dim_rot[i]!= 0.0)
        {
            if ((abs(dim1[i] - dim_rot[i]) < threshold))
            //if ((abs(dim1[i] - dim_rot[i])/dim1[i] < threshold))
                equal=equal && true;
            else
                equal=false;
        }
    }

    return equal;
}*/

/****************************************************************/
double SuperqComputation::edgesClose(node *node1, node *node2)
{
    deque<Vector> edges_1;
    deque<Vector> edges_2;

    computeEdges(node1, edges_1);
    computeEdges(node2, edges_2);

    double distance_min=1000.0;

    for (size_t i=0; i<edges_1.size(); i++)
    {
       for (size_t j=0; j<edges_2.size(); j++)
       {
           double distance=norm(edges_1[i]-edges_2[j]);

           if (distance < distance_min)
           {
               distance_min=distance;
           }
       }

    }

    return distance_min;

}

/****************************************************************/
bool SuperqComputation::computeEdges(node *node, deque<Vector> &edges)
{
    edges.clear();

    Vector point(3,0.0);

    point = node->superq.subVector(5,7) + node->superq[0] * node->axis_x.subVector(0,2);
    edges.push_back(point);

    point = node->superq.subVector(5,7) - node->superq[0] * node->axis_x.subVector(0,2);
    edges.push_back(point);

    point = node->superq.subVector(5,7) + node->superq[1] * node->axis_y.subVector(0,2);
    edges.push_back(point);

    point = node->superq.subVector(5,7) - node->superq[1] * node->axis_y.subVector(0,2);
    edges.push_back(point);

    point = node->superq.subVector(5,7) + node->superq[2] * node->axis_z.subVector(0,2);
    edges.push_back(point);

    point = node->superq.subVector(5,7) - node->superq[2] * node->axis_z.subVector(0,2);
    edges.push_back(point);

    if (debug)
    {
        yDebug()<<"Edges "<<edges.size();
        for (size_t i=0; i< edges.size(); i++)
            yDebug()<<edges[i].toString();
    }

    return true;


}
