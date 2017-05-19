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
#include <string>
#include <sstream>
#include <deque>
#include <map>
#include <set>
#include <fstream>
#include <iomanip>

#include <yarp/os/all.h>
#include <yarp/dev/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/Math.h>

#include <opencv2/opencv.hpp>


#include "src/testingModule_IDL.h"

using namespace std;
using namespace yarp::os;
using namespace yarp::dev;
using namespace yarp::sig;
using namespace yarp::math;

class AcquireBlob : public RFModule,
                    testingModule_IDL
{
    string method;
    string objname;
    bool streaming;

    vector<cv::Point> object_center;
    deque<cv::Point> blob_points;

    RpcClient portBlobRpc;
    RpcClient portOPCrpc;
    RpcClient superqRpc;
    RpcServer portRpc;

    BufferedPort<Bottle > blobPort;

    Mutex mutex;

public:

    /************************************************************************/
    bool attach(RpcServer &source)
    {
        return this->yarp().attachAsServer(source);
    }

    /************************************************************************/
    Bottle  get_Blob()
    {
        Bottle blob;
        for (size_t i=0; i<blob_points.size(); i++)
        {
            Bottle &b=blob.addList();
            b.addDouble(blob_points[i].x); b.addDouble(blob_points[i].y);
        }

        return blob;
    }

    /************************************************************************/
    bool  set_streaming_mode(const string &entry)
    {
        if (entry=="on" || entry=="off")
        {
            streaming=(entry=="on");
            return true;
        }
        else
            return false;
    }

    /************************************************************************/
    void  sendBlob()
    {
        Bottle &blob=blobPort.prepare();
        blob.clear();
        Bottle &b1=blob.addList();
        
        for (size_t i=0; i<blob_points.size(); i++)
        {
            Bottle &b=b1.addList();
            b.addDouble(blob_points[i].x); b.addDouble(blob_points[i].y);
        }

        blobPort.write();
        
    }

    /************************************************************************/
    bool getperiod()
    {
        return 0.0;
    }

    /************************************************************************/
    bool set_object_name(const string &object_name)
    {
        LockGuard lg(mutex);
        objname=object_name;
        method="name";

        return true;
    }

    /**********************************************************************/
    bool configure(ResourceFinder &rf)
    {
        method=rf.find("method").asString().c_str();
        if (rf.find("method").isNull())
            method="name";

        objname=rf.find("object_name").asString().c_str();
        if (rf.find("object_name").isNull())
            objname="object";

        streaming=(rf.check("streaming", Value("off")).asString()=="on");

        portBlobRpc.open("/testing-module/blob:rpc");
        portOPCrpc.open("/testing-module/OPC:rpc");
        superqRpc.open("/testing-module/superq:rpc");
        portRpc.open("/testing-module/rpc");

        blobPort.open("/testing-module/blob:o");

        attach(portRpc);
        return true;
    }

    /**********************************************************************/
    bool close()
    {

        if (portBlobRpc.asPort().isOpen())
            portBlobRpc.close();
        if (portOPCrpc.asPort().isOpen())
            portOPCrpc.close();
        if (superqRpc.asPort().isOpen())
            superqRpc.close();

        return true;
    }

    /**********************************************************************/
    bool updateModule()
    {
        if (method=="point")
        {
            blob_points.clear();

            if (object_center.size()>0)
            {
                getBlob();
            }
            else
            {
                blob_points.push_back(cv::Point(0,0));
            }
        }
        else if (method=="name")
        {
            pointFromName();

            if ((object_center.size()>0) )
            {
                getBlob();
            }
            else
            {
                blob_points.push_back(cv::Point(0,0));
            }
        }

        if (blob_points.size()>0 && streaming==true)
            sendBlob();
        else if (blob_points.size()>0 && (streaming==false))
        {
            Bottle cmd, reply;
            cmd.addString("get_superq");

            Bottle &in1=cmd.addList();
        
            for (size_t i=0; i<blob_points.size(); i++)
            {
                Bottle &in=in1.addList();
                in.addDouble(blob_points[i].x);                        
                in.addDouble(blob_points[i].y);
            }
            
            // Add 1 instead of 0 if you want the filtered superquadric
            cmd.addInt(0);

            superqRpc.write(cmd, reply);

            cout<<"Received superquadric: "<<reply.toString()<<endl;
        }

        return true;
    }

    /***********************************************************************/
    void getBlob()
    {
        Bottle cmd,reply;
        blob_points.clear();
        
        cmd.addString("get_component_around");
        cmd.addInt(object_center[0].x); cmd.addInt(object_center[0].y);

        if (portBlobRpc.write(cmd,reply))
        {
            
            if (Bottle *blob_list=reply.get(0).asList())
            {
                for (int i=0; i<blob_list->size();i++)
                {
                    if (Bottle *blob_pair=blob_list->get(i).asList())
                    {
                        blob_points.push_back(cv::Point(blob_pair->get(0).asInt(),blob_pair->get(1).asInt()));
                    }
                    else
                    {
                        yError()<<"Some problems in blob pixels!";
                    }
                }
            }
            else
            {
                yError()<<"Some problem  in object blob!";
            }
        }
        else
        {
            yError("lbpExtract query is fail!");
        }
    }

    /***********************************************************************/
    void pointFromName()
    {
        Bottle cmd,reply;
        blob_points.clear();
        object_center.clear();
        cmd.addVocab(Vocab::encode("ask"));
        Bottle &content=cmd.addList();
        Bottle &cond_1=content.addList();
        cond_1.addString("entity");
        cond_1.addString("==");
        cond_1.addString("object");
        content.addString("&&");
        Bottle &cond_2=content.addList();
        cond_2.addString("name");
        cond_2.addString("==");
        cond_2.addString(objname);

        portOPCrpc.write(cmd,reply);
        if(reply.size()>1)
        {
            if(reply.get(0).asVocab()==Vocab::encode("ack"))
            {
                if (Bottle *b=reply.get(1).asList())
                {
                    if (Bottle *b1=b->get(1).asList())
                    {
                        cmd.clear();
                        int id=b1->get(0).asInt();
                        cmd.addVocab(Vocab::encode("get"));
                        Bottle &info=cmd.addList();
                        Bottle &info2=info.addList();
                        info2.addString("id");
                        info2.addInt(id);
                        Bottle &info3=info.addList();
                        info3.addString("propSet");
                        Bottle &info4=info3.addList();
                        info4.addString("position_2d_left");
                    }
                    else
                    {
                        yError("no object id provided by OPC!");
                        object_center.clear();
                    }
                }
                else
                {
                    yError("uncorrect reply from OPC!");
                    object_center.clear();
                }

                Bottle reply;
                if (portOPCrpc.write(cmd,reply))
                {

                    if (reply.size()>1)
                    {
                        if (reply.get(0).asVocab()==Vocab::encode("ack"))
                        {
                            if (Bottle *b=reply.get(1).asList())
                            {
                                if (Bottle *b1=b->find("position_2d_left").asList())
                                {
                                    cv::Point p1,p2,p;
                                    p1.x=b1->get(0).asInt();
                                    p1.y=b1->get(1).asInt();
                                    p2.x=b1->get(2).asInt();
                                    p2.y=b1->get(3).asInt();
                                    p.x=p1.x+(p2.x-p1.x)/2;
                                    p.y=p1.y+(p2.y-p1.y)/2;
                                    object_center.push_back(p);
                                }
                                else
                                {
                                    yError("position_2d_left field not found in the OPC reply!");
                                    object_center.clear();
                                }
                            }
                            else
                            {
                                yError("uncorrect reply structure received!");
                                object_center.clear();
                            }
                        }
                        else
                        {
                            yError("Failure in reply for object 2D point!");
                            object_center.clear();
                        }
                    }
                    else
                    {
                        yError("reply size for 2D point less than 1!");
                        object_center.clear();
                    }
                }
                else
                    yError("no reply from second OPC query!");
            }
            else
            {
                yError("Failure in reply for object id!");
                object_center.clear();
            }
        }
        else
        {
            yError("reply size for object id less than 1!");
            object_center.clear();
        }
    }
};

/**********************************************************************/
int main(int argc,char *argv[])
{
    Network yarp;
    if (!yarp.checkNetwork())
    {
        yError("unable to find YARP server!");
        return 1;
    }

    AcquireBlob mod;
    ResourceFinder rf;
    rf.setDefaultContext("testing-module");
    rf.configure(argc,argv);
    return mod.runModule(rf);
}
