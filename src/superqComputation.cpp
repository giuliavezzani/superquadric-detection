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
SuperqComputation::SuperqComputation(int _rate, bool _filter_points, bool _filter_superq, bool _fixed_window, string _tag_file, double _threshold_median,
                                const Property &_filter_points_par, const Property &_filter_superq_par, const Property &_ipopt_par, const string &_homeContextPath, bool _save_points):
                                filter_points(_filter_points), filter_superq(_filter_superq), fixed_window(_fixed_window),tag_file(_tag_file),  threshold_median(_threshold_median), save_points(_save_points),
                                filter_points_par(_filter_points_par),filter_superq_par(_filter_superq_par),ipopt_par(_ipopt_par), RateThread(_rate), homeContextPath(_homeContextPath)
{
}

/***********************************************************************/
void SuperqComputation::setPointsFilterPar(const Property &newOptions)
{
    LockGuard lg(mutex);

    double radiusValue=newOptions.find("filter_radius").asDouble();
    if (newOptions.find("filter_radius").isNull())
    {
        radius=0.01;
    }
    else
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
    if (newOptions.find("filter_nnThreshold").isNull())
    {
        nnThreshold=100;
    }
    else
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
    LockGuard lg(mutex);

    Property advOptions;
    advOptions.put("filter_radius",radius);
    advOptions.put("filter_nnThreshold",nnThreshold);
    return advOptions;
}

/***********************************************************************/
void SuperqComputation::setSuperqFilterPar(const Property &newOptions)
{
    LockGuard lg(mutex);

    int mOrderValue=newOptions.find("median_order").asInt();
    if (newOptions.find("median_order").isNull())
    {
        std_median_order=3;
    }
    else
    {
        if((mOrderValue>=1) && (mOrderValue<=50))
        {
            std_median_order=mOrderValue;
        }
        else
            std_median_order=3;
    }

    mOrderValue=newOptions.find("min_median_order").asInt();
    if (newOptions.find("min_median_order").isNull())
    {
        min_median_order=1;
    }
    else
    {
        if ((mOrderValue>=1) && (mOrderValue<=50))
        {
            min_median_order=mOrderValue;
        }
        else
            min_median_order=1;
    }

    mOrderValue=newOptions.find("max_median_order").asInt();
    if (newOptions.find("max_median_order").isNull())
    {
        max_median_order=30;
    }
    else
    {
        if ((mOrderValue>=1) && (mOrderValue<=50))
        {
            max_median_order=mOrderValue;
        }
        else
            max_median_order=1;
    }

    double threValue=newOptions.find("threshold_median").asDouble();
    if (newOptions.find("threhsold_median").isNull())
    {
        threshold_median=0.1;
    }
    else
    {
        if ((threValue>0.005) && (threValue<=2.0))
        {
            threshold_median=threValue;
        }
        else
        {
            threshold_median=0.1;
        }
    }

    double minNormVel=newOptions.find("min_norm_vel").asDouble();
    if (newOptions.find("min_norm_vel").isNull())
    {
        min_norm_vel=0.01;
    }
    else
    {
        if ((minNormVel>0.005) && (minNormVel<=0.1))
        {
            min_norm_vel=minNormVel;
        }
        else
        {
            min_norm_vel=0.01;
        }
    }

    string par=newOptions.find("fixed_window").asString();
    if (newOptions.find("fixed_window").isNull())
    {
        fixed_window=false;
    }
    else
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
    LockGuard lg(mutex);

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
void SuperqComputation::setIpoptPar(const Property &newOptions)
{
    LockGuard lg(mutex);

    int points=newOptions.find("optimizer_points").asInt();


    if (newOptions.find("optimizer_points").isNull())
    {
        optimizer_points=50;
    }
    else
    {
        if ((points>=1) && (points<=300))
        {
            optimizer_points=points;
        }
        else
        {
            optimizer_points=50;
        }
    }

    double maxCpuTime=newOptions.find("max_cpu_time").asDouble();
    if (newOptions.find("max_cpu_time").isNull())
    {
        max_cpu_time=5.0;
    }
    else
    {
        if ((maxCpuTime>=0.01) && (maxCpuTime<=10.0))
        {
            max_cpu_time=maxCpuTime;
        }
        else
        {
            max_cpu_time=5.0;
        }
    }

    double tolValue=newOptions.find("tol").asDouble();
    if (newOptions.find("tol").isNull())
    {
        tol=1e-5;
    }
    else
    {
        if ((tolValue>1e-8) && (tolValue<=0.01))
        {
            tol=tolValue;
        }
        else
        {
            tol=1e-5;
        }
    }

    int accIter=newOptions.find("acceptable_iter").asInt();
    if (newOptions.find("acceptable_iter").isNull())
    {
        acceptable_iter=0;
    }
    else
    {
        if ((accIter>=0 )&& (accIter<=100))
        {
             acceptable_iter=accIter;
        }
        else
        {
            acceptable_iter=0;
        }
    }

    int maxIter=newOptions.find("max_iter").asInt();
    if (newOptions.find("max_iter").isNull())
    {
        max_iter=100;
    }
    else
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
    if (newOptions.find("mu_strategy").isNull())
    {
        mu_strategy="monotone";
    }
    else
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
    if (newOptions.find("nlp_scaling_method").isNull())
    {
        nlp_scaling_method="gradient-based";
    }
    else
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
    LockGuard lg(mutex);
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
}

/***********************************************************************/
double SuperqComputation::getTime()
{   
    LockGuard lg(mutex);
    return t_superq;
}

/***********************************************************************/
bool SuperqComputation::threadInit()
{
    yInfo()<<"[SuperqComputation]: Thread initing ... ";

    if (filter_points==true)
        setPointsFilterPar(filter_points_par);

    if (filter_superq==true)
        setSuperqFilterPar(filter_superq_par);
    
    setIpoptPar(ipopt_par);

    configFilterSuperq();
    config3Dpoints();

    bounds_automatic=true;
    one_shot=false;
    
    x.resize(11,0.00);
    x_filtered.resize(11,0.0);

    return true;
}

/***********************************************************************/
void SuperqComputation::run()
{
    t0=Time::now();
    LockGuard lg(mutex);

    acquirePointsFromBlob(imgIn);

    if ((filter_points==true) && (points.size()>0))
    {
        filter();
    }

    if (points.size()>0)
    {
        yInfo()<<"[SuperqComputation]: number of points acquired:"<< points.size();
        go_on=computeSuperq();
    }

    if ((go_on==false) && (points.size()>0))
    {
        yError("[SuperqComputation]: Not found a suitable superquadric! ");
    }
    else if (go_on==true && norm(x)>0.0 && (points.size()>0))
    {
        if (filter_superq)
            filterSuperq();
        else
            x_filtered=x;
    }
    else
    {
        x_filtered.resize(11,0.0);
    }

    t_superq=Time::now() - t0;
}

/***********************************************************************/
void SuperqComputation::threadRelease()
{
    yInfo()<<"[SuperComputation]: Thread releasing ... ";

    if (portSFMrpc.asPort().isOpen())
        portSFMrpc.close();

    if (!blobPort.isClosed())
        blobPort.close();

    if (mFilter!=NULL)
        delete mFilter;

    if (PolyEst!=NULL)
        delete PolyEst;
}

/***********************************************************************/
bool SuperqComputation::configFilterSuperq()
{
    x.resize(11,0.0);
    new_median_order=1;
    elem_x.resize(max_median_order, 0.0);
    std_median_order=5;
    max_median_order=30;
    mFilter = new MedianFilter(median_order, x);
    PolyEst =new AWLinEstimator(max_median_order, threshold_median);
    return true;
}

/***********************************************************************/
bool SuperqComputation::config3Dpoints()
{
    blobPort.open("/superquadric-model/blob:i");
    portSFMrpc.open("/superquadric-model/SFM:rpc");

    return true;
}

/***********************************************************************/
void SuperqComputation::acquirePointsFromBlob(ImageOf<PixelRgb>  *ImgIn)
{
    if (!one_shot)
        getBlob();

    if (blob_points.size()>1)
    {
        get3Dpoints(ImgIn);
    }
}

/***********************************************************************/
void SuperqComputation::getBlob()
{
    Bottle *reply;   
    points.clear();
        
    reply=blobPort.read(false);   

    if (reply!=NULL)
    {  
        blob_points.clear();

        if (Bottle *blob_list=reply->get(0).asList())
        {
            if (blob_list->size()>1)
            {
                for (int i=0; i<blob_list->size();i++)
                {
                    if (Bottle *blob_pair=blob_list->get(i).asList())
                    {
                        blob_points.push_back(cv::Point(blob_pair->get(0).asInt(),blob_pair->get(1).asInt()));
                    }
                    else
                    {
                        yError()<<"[SuperqComputation]: Some problems in blob pixels!";
                    }
                }
            }
            else
                yError()<<"[SuperqComputation]: Blob size equal to 1!";
        }
        else
        {
            yError()<<"[SuperqComputation]: Some problem  in object blob!";
        }
    }
    else
    {       
        yError("[SuperqComputation]: 2D blob not received!");
    }
}

/***********************************************************************/
void SuperqComputation::get3Dpoints(ImageOf<PixelRgb>  *ImgIn)
{
    Bottle cmd,reply;
    cmd.addString("Points");
    count=0;
    int count_blob=0;

    for (size_t i=0; i<blob_points.size(); i++)
    {
        cv::Point single_point=blob_points[i];
        cmd.addInt(single_point.x);
        cmd.addInt(single_point.y);
    }

    if (portSFMrpc.write(cmd,reply))
    {
        count_blob=0;

        for (int idx=0;idx<reply.size();idx+=3)
        {
            Vector point(6,0.0);
            point[0]=reply.get(idx+0).asDouble();
            point[1]=reply.get(idx+1).asDouble();
            point[2]=reply.get(idx+2).asDouble();
            count++;

            /*PixelRgb px=imgIn->pixel(cmd.get(count_blob+1).asInt(),cmd.get(count_blob).asInt());
            point[3]=px.r;
            point[4]=px.g;
            point[5]=px.b;*/

            count_blob+=2;

            if ((norm(point)>0))
            {
                points.push_back(point);
                count=0;
            }
        }

        if (points.size()<=0)
        {
            yError("[SuperqComputation]: Some problems in point acquisition!");
        }
        else
        {
            Vector colors(3,0.0);
            colors[0]=255;
            if (save_points)
            {
                yInfo()<<"[SuperqComputation]: Saving point cloud ... ";
                savePoints("/SFM-"+tag_file, colors);
            }
        }
    }
    else
    {
        yError("[SuperqComputation]: SFM reply is fail!");
        points.clear();
    }
}

/***********************************************************************/
void SuperqComputation::savePoints(const string &namefile, const Vector &colors)
{
    ofstream fout;
    fout.open((homeContextPath+namefile+".off").c_str());

    if (fout.is_open())
    {
        fout<<"COFF"<<endl;
        fout<<points.size()<<" 0 0"<<endl;
        fout<<endl;
        for (size_t i=0; i<points.size(); i++)
        {
            fout<<points[i].subVector(0,2).toString(3,4).c_str()<<" "<<
                    (int)points[i][3]<<" "<<(int)points[i][4]<<" "<<(int)points[i][5]<<endl;
        }

        fout<<endl;
    }
    else
        yError()<<"[SuperqComputation]: Some problems in opening output file!";

    fout.close();
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
bool SuperqComputation::computeSuperq()
{
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
    superQ_nlp->configure(bounds_automatic);

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
void SuperqComputation::filterSuperq()
{
    yInfo()<< "[SuperqComputation]: Filtering the last "<< median_order << " superquadrics...";

    yInfo()<<"[SuperqComputation]: x "<<x.toString();

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

        x_filtered=mFilter->filt(x);
    }

    if (norm(x_filtered)==0.0)
        x_filtered=x;

    yInfo()<< "[SuperqComputation]: Filtered superq "<< x_filtered.toString(3,3);
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
void SuperqComputation::sendImg(ImageOf<PixelRgb> *Img)
{
    LockGuard lg(mutex);
    imgIn=Img;
}

/***********************************************************************/
void SuperqComputation::setContour(cv::Point p)
{
    LockGuard lg(mutex);
    contour.push_back(p);
}

/***********************************************************************/
void SuperqComputation::getPoints(deque<Vector> &p)
{
    LockGuard lg(mutex);

    p.clear();

    for (size_t i=0; i<points.size(); i++)
    {   
        p.push_back(points[i]);
    }
}

/***********************************************************************/
void SuperqComputation::sendPoints(deque<Vector> &p)
{
    LockGuard lg(mutex);

    points.clear();
    for (size_t i=0; i<p.size(); i++)
    {
        points.push_back(p[i]);
    }
}

/***********************************************************************/
void SuperqComputation::sendBlobPoints(const vector<Vector> &p)
{
    LockGuard lg(mutex);

    blob_points.clear();
    for (size_t i=0; i<p.size(); i++)
    {
        Vector tmp=p[i];
        blob_points.push_back(cv::Point(tmp[0],tmp[1]));
    }
}



