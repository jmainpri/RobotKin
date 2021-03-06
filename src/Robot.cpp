/*
 -------------------------------------------------------------------------------
 Robot.cpp
 robotTest Project
 
 Initially created by Rowland O'Flaherty ( rowlandoflaherty.com ) on 5/15/13.
 
 Version 1.0
 -------------------------------------------------------------------------------
 */



//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------
#include "Robot.h"



//------------------------------------------------------------------------------
// Namespaces
//------------------------------------------------------------------------------
using namespace std;
using namespace Eigen;
using namespace RobotKin;


//------------------------------------------------------------------------------
// Robot Lifecycle
//------------------------------------------------------------------------------
// Constructors
Robot::Robot()
        : Frame::Frame(Isometry3d::Identity()),
          respectToWorld_(Isometry3d::Identity()),
          initializing_(false)
{
    linkages_.resize(0);
    frameType_ = ROBOT;
}

Robot::Robot(vector<Linkage> linkageObjs, vector<int> parentIndices)
        : Frame::Frame(Isometry3d::Identity()),
          respectToWorld_(Isometry3d::Identity()),
          initializing_(false)
{
    frameType_ = ROBOT;
    
    linkages_.resize(0);
    initialize(linkageObjs, parentIndices);
}

#ifdef HAVE_URDF_PARSE
Robot::Robot(string filename, string name, size_t id)
    : Frame::Frame(Isometry3d::Identity(), name, id, ROBOT),
      respectToWorld_(Isometry3d::Identity()),
      initializing_(false)
{
    // TODO: Test to make sure filename ends with ".urdf"
    linkages_.resize(0);
    loadURDF(filename);
}

bool Robot::loadURDF(string filename)
{
    return RobotKinURDF::loadURDF(*this, filename);
}

#endif // HAVE_URDF_PARSE


// Destructor
Robot::~Robot()
{
    
}


//--------------------------------------------------------------------------
// Robot Public Member Functions
//--------------------------------------------------------------------------
size_t Robot::nLinkages() const { return linkages_.size(); }

size_t Robot::linkageIndex(string linkageName) const
{
    map<string,size_t>::const_iterator j = linkageNameToIndex_.find(linkageName);
    if( j != linkageNameToIndex_.end() )
        return j->second;

    return 0;
}

const Linkage& Robot::const_linkage(size_t linkageIndex) const
{ // FIXME: Remove assert
    return *linkages_[linkageIndex];
}
const Linkage& Robot::const_linkage(string linkageName) const { return *linkages_[linkageNameToIndex_.at(linkageName)]; }

Linkage& Robot::linkage(size_t linkageIndex)
{ // FIXME: Remove assert
    if(linkageIndex < nJoints())
        return *linkages_[linkageIndex];

    Linkage* invalidLinkage = new Linkage;
    invalidLinkage->name("invalid");
    return *invalidLinkage;
}
Linkage& Robot::linkage(string linkageName)
{
    map<string,size_t>::iterator j = linkageNameToIndex_.find(linkageName);
    if( j != linkageNameToIndex_.end() )
        return *linkages_.at(j->second);

    Linkage* invalidLinkage = new Linkage;
    invalidLinkage->name("invalid");
    return *invalidLinkage;
}

const vector<Linkage*>& Robot::const_linkages() const { return linkages_; }

vector<Linkage*>& Robot::linkages() { return linkages_; }

size_t Robot::nJoints() const { return joints_.size(); }

size_t Robot::jointIndex(string jointName) const { return jointNameToIndex_.at(jointName); }

const Linkage::Joint& Robot::const_joint(size_t jointIndex) const
{
    if(jointIndex < nJoints())
        return *joints_[jointIndex];

    Linkage::Joint* invalidJoint = new Linkage::Joint;
    invalidJoint->name("invalid");
    return *invalidJoint;
}
const Linkage::Joint& Robot::const_joint(string jointName) const
{
    map<string,size_t>::const_iterator j = jointNameToIndex_.find(jointName);
    if( j != jointNameToIndex_.end() )
        return *joints_.at(j->second);

    Linkage::Joint* invalidJoint = new Linkage::Joint;
    invalidJoint->name("invalid");
    return *invalidJoint;
}

Linkage::Joint& Robot::joint(size_t jointIndex)
{
    if(jointIndex < nJoints())
        return *joints_[jointIndex];

    Linkage::Joint* invalidJoint = new Linkage::Joint;
    invalidJoint->name("invalid");
    return *invalidJoint;
}
Linkage::Joint& Robot::joint(string jointName)
{
    map<string,size_t>::const_iterator j = jointNameToIndex_.find(jointName);
    if( j != jointNameToIndex_.end() )
        return *joints_.at(j->second);

    Linkage::Joint* invalidJoint = new Linkage::Joint;
    invalidJoint->name("invalid");
    return *invalidJoint;
}

const vector<Linkage::Joint*>& Robot::const_joints() const { return joints_; }

vector<Linkage::Joint*>& Robot::joints() { return joints_; }

VectorXd Robot::values() const
{
    VectorXd theValues(nJoints(),1);
    for (size_t i = 0; i < nJoints(); ++i) {
        theValues[i] = joints_[i]->value();
    }
    return theValues;
}

void Robot::values(const VectorXd& someValues) {

    if(someValues.size() == nJoints())
    {
        for (size_t i = 0; i < nJoints(); ++i) {
            joints_[i]->value(someValues(i));
        }
        updateFrames();
    }
    else
        cerr << "Invalid number of joint values: " << someValues.size()
             << "\n\t This should be equal to " << nJoints()
             << "\n\t See line (" << __LINE__-10 << ") of Robot.cpp"
             << endl;
}

void Robot::values(const vector<size_t>& jointIndices, const VectorXd& jointValues)
{
    if( jointIndices.size() == jointValues.size() )
    {
        for(size_t i=0; i<jointIndices.size(); i++)
            joints_[jointIndices[i]]->value(jointValues[i]);
        updateFrames();
    }
    else
        cerr << "Invalid number of joint values: " << jointIndices.size()
             << "\n\t This should be equal to " << jointValues.size()
             << "\n\t See line (" << __LINE__-8 << ") of Robot.cpp"
             << endl;
}

void Robot::setJointValue(string jointName, double val){joint(jointName).value(val);}

void Robot::setJointValue(size_t jointIndex, double val){joint(jointIndex).value(val);}

const Isometry3d& Robot::respectToFixed() const { return respectToFixed_; }
void Robot::respectToFixed(Isometry3d aCoordinate)
{
    respectToFixed_ = aCoordinate;
    updateFrames();
}

Isometry3d Robot::respectToWorld() const
{
    return respectToWorld_;
}

void Robot::jacobian(MatrixXd& J, const vector<Linkage::Joint*>& jointFrames, Vector3d location, const Frame* refFrame) const
{ // location should be specified in respect to robot coordinates
    size_t nCols = jointFrames.size();
    J.resize(6, nCols);
    
    Vector3d o_i, d_i, z_i; // Joint i location, offset, axis
    
    for (size_t i = 0; i < nCols; i++) {

        o_i = jointFrames[i]->respectToRobot().translation(); // Joint i location
        d_i = location - o_i; // Changing convention so that the position vector points away from the joint axis
        z_i = jointFrames[i]->respectToRobot().rotation()*jointFrames[i]->jointAxis_;

//        cout << jointFrames[i]->name() << " " << d_i.transpose() << " : " << z_i.transpose() << endl;
        
        // Set column i of Jocabian
        if (jointFrames[i]->jointType_ == REVOLUTE) {
            J.block(0, i, 3, 1) = z_i.cross(d_i);
            J.block(3, i, 3, 1) = z_i;
        } else if(jointFrames[i]->jointType_ == PRISMATIC) {
            J.block(0, i, 3, 1) = z_i;
            J.block(3, i, 3, 1) = Vector3d::Zero();
        } else {
            J.block(0, i, 3, 1) = Vector3d::Zero();
            J.block(3, i, 3, 1) = Vector3d::Zero();
        }
        
    }
    
    // Jacobian transformation
    Matrix3d r(refFrame->respectToWorld().rotation().inverse() * respectToWorld_.rotation());
    MatrixXd R(6,6);
    R << r, Matrix3d::Zero(), Matrix3d::Zero(), r;
    J = R * J;
}

void Robot::printInfo() const
{
    Frame::printInfo();
    
    cout << "Linkages (ID, Name <- Parent): " << endl;
    for (vector<Linkage*>::const_iterator linkageIt = const_linkages().begin();
         linkageIt != const_linkages().end(); ++linkageIt) {
        if ((*linkageIt)->parentLinkage_ == 0) {
            cout << (*linkageIt)->id() << ", " << (*linkageIt)->name() << " <- " << this->name() << endl;
        } else {
            cout << (*linkageIt)->id() << ", " << (*linkageIt)->name() << " <- " << (*linkageIt)->parentLinkage_->name() << endl;
        }
    }
    cout << "Joints (ID, Name, Value): " << endl;
    for (vector<Linkage::Joint*>::const_iterator jointIt = const_joints().begin();
         jointIt != const_joints().end(); ++jointIt) {
        cout << (*jointIt)->id() << ", " << (*jointIt)->name() << ", " << (*jointIt)->value() << endl;
    }
    for (vector<Linkage*>::const_iterator linkageIt = const_linkages().begin();
         linkageIt != const_linkages().end(); ++linkageIt) {
        (*linkageIt)->printInfo();
    }
}

//------------------------------------------------------------------------------
// Robot Protected Member Functions
//------------------------------------------------------------------------------
void Robot::initialize(vector<Linkage> linkages, vector<int> parentIndices)
{
    
    if(linkages.size() != parentIndices.size())
    {
        std::cerr << "ERROR! Number of linkages (" << linkages.size() << ") does not match "
                  << "the number of parent indices (" << parentIndices.size() << ")!"
                  << std::endl;
        return;
    }
    
    vector<indexParentIndexPair> iPI(linkages.size());
    for (size_t i = 0; i != linkages.size(); ++i) {
        iPI[i].I = i;
        iPI[i].pI = parentIndices[i];
    }
    sort(iPI.begin(), iPI.end());
    // ^^^ Make sure that parents get added before children
    
    
    initializing_ = true;


    for(size_t i = 0; i != linkages.size(); ++i)
        addLinkage(linkages[iPI[i].I], parentIndices[iPI[i].I], linkages[iPI[i].I].name());
    
    initializing_ = false;
    
    
    updateFrames();
}

void Robot::addLinkage(Linkage linkage, int parentIndex, string name)
{
    // Get the linkage adjusted to its new home
    size_t newIndex = linkages_.size();

    Linkage* tempLinkage = new Linkage(linkage);
    linkages_.push_back(tempLinkage);
    
    if(parentIndex == -1)
        linkages_[newIndex]->parentLinkage_ = NULL;
    else if( parentIndex > linkages_.size()-1 )
    {
        std::cerr << "ERROR! Parent index value (" << parentIndex << ") is larger "
                  << "than the current highest linkage index (" << linkages().size()-1 << ")!"
                  << std::endl;
        return;
    }
    else
        linkages_[newIndex]->parentLinkage_ = linkages_[parentIndex];
    
    linkages_[newIndex]->hasParent = true; // TODO: Decide if this should be true for root linkage or not
    

    linkages_[newIndex]->robot_ = this;
    linkages_[newIndex]->hasRobot = true;
    linkages_[newIndex]->id_ = newIndex;
    linkages_[newIndex]->name_ = name;
    // Move in its luggage
    for(size_t j = 0; j != linkages_[newIndex]->nJoints(); ++j)
    {
        linkages_[newIndex]->joints_[j]->linkage_ = linkages_[newIndex];
        linkages_[newIndex]->joints_[j]->hasLinkage = true;
        linkages_[newIndex]->joints_[j]->robot_ = this;
        linkages_[newIndex]->joints_[j]->hasRobot = true;
        joints_.push_back(linkages_[newIndex]->joints_[j]);
        joints_.back()->id_ = joints_.size()-1;
        jointNameToIndex_[joints_.back()->name()] = joints_.size()-1;
    }
    // TODO: Allow for multiple tools maybe?
    linkages_[newIndex]->tool_.linkage_ = linkages_[newIndex];
    linkages_[newIndex]->tool_.hasLinkage = true;
    linkages_[newIndex]->tool_.robot_ = this;
    linkages_[newIndex]->tool_.hasRobot = true;
    linkages_[newIndex]->tool_.id_ = newIndex;
    
    // Tell the post office we've moved in
    linkageNameToIndex_[linkages_[newIndex]->name_] = newIndex;
    
    // Inform the parent of its pregnancy
    if(linkages_[newIndex]->parentLinkage_ != NULL)
    {
        linkages_[newIndex]->parentLinkage_->childLinkages_.push_back(linkages_[newIndex]);
        linkages_[newIndex]->parentLinkage_->hasChildren = true;
    }
}

void Robot::addLinkage(int parentIndex, string name)
{
    Linkage invalidLinkage;
    addLinkage(invalidLinkage, parentIndex, name);
}

//------------------------------------------------------------------------------
// Robot Private Member Functions
//------------------------------------------------------------------------------
void Robot::updateFrames()
{
    if (~initializing_) {
        for (vector<Linkage*>::iterator linkageIt = linkages_.begin();
             linkageIt != linkages_.end(); ++linkageIt) {
            
            if ((*linkageIt)->parentLinkage_ == 0) {
                (*linkageIt)->respectToRobot_ = (*linkageIt)->respectToFixed_;
            } else {
                (*linkageIt)->respectToRobot_ = (*linkageIt)->parentLinkage_->tool_.respectToRobot() * (*linkageIt)->respectToFixed_;
            }
        }
    }
}




