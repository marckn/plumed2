#include "Action.h"
#include "PlumedMain.h"
#include "Log.h"
#include <cassert>

using namespace PLMD;

ActionOptions::ActionOptions(PlumedMain&p,const std::vector<std::string>&l):
plumed(p),
line(l)
{
}

Action::Action(const ActionOptions&ao):
  name(ao.line[0]),
  line(ao.line),
  active(false),
  plumed(ao.plumed),
  log(plumed.getLog()),
  comm(plumed.comm)
{
  line.erase(line.begin());
  log.printf("Action %s\n",name.c_str());
  parse("LABEL",label);
  if(label.length()==0){
    std::string s; Tools::convert(plumed.getActionSet().size(),s);
    label="@"+s;
  }
  assert(!plumed.getActionSet().selectWithLabel<Action*>(label));
  log.printf("  with label %s\n",label.c_str());
}

FILE* Action::fopen(const char *path, const char *mode){
  bool write(false);
  for(const char*p=mode;*p;p++) if(*p=='w') write=true;
  FILE* fp;
  if(write && comm.Get_rank()!=0) fp=plumed.fopen("/dev/null",mode);
  else      fp=plumed.fopen(path,mode);
  files.insert(fp);
  return fp;
}

int Action::fclose(FILE*fp){
  files.erase(fp);
  return plumed.fclose(fp);
}

void Action::fflush(){
  for(files_iterator p=files.begin();p!=files.end();++p){
    std::fflush((*p));
  }
}

void Action::parseFlag(const std::string&key,bool & t){
  if(!Tools::parseFlag(line,key,t)){
    log.printf("ERROR parsing keyword %s\n",key.c_str());
    log.printf("%s\n",getDocumentation().c_str());
    this->exit(1);
  }
}

void Action::addDependency(Action*action){
  assert(this->after.count(action)==action->before.count(this));
  this->after.insert(action);
  action->before.insert(this);
}

void Action::activate(){
// preparation step is called only the first time an Action is activated.
// since it could change its dependences (e.g. in an ActionAtomistic which is
// accessing to a virtual atom), this is done just before dependencies are
// activated
  if(!active){
    this->unlockRequests();
    prepare();
    this->lockRequests();
  }
  for(Dependencies::iterator p=after.begin();p!=after.end();p++) (*p)->activate();
  active=true;
}

void Action::clearDependencies(){
  for(Dependencies::iterator p=after.begin();p!=after.end();p++){
    (*p)->before.erase(this);
  };
  this->after.clear();
}

std::string Action::getDocumentation()const{
  return std::string("UNDOCUMENTED ACTION");
}

void Action::checkRead(){
  if(line.size()>0){
    log.printf("ERROR READING INPUT FILE\n");
    log.printf("I CANNOT UNDERSTAND THE FOLLOWING WORDS:\n");
    for(unsigned i=0;i<line.size();i++) log.printf("  %s\n",line[i].c_str());
    log.printf("STOP!!\n");
    exit(1);
  }
}

int Action::getStep()const{
  return plumed.getStep();
}

double Action::getTime()const{
  return plumed.getAtoms().getTimeStep()*getStep();
}

double Action::getTimeStep()const{
  return plumed.getAtoms().getTimeStep();
}



void Action::exit(int c){
  plumed.exit(c);
}

void Action::calculateNumericalDerivatives(){
  assert(0);
}

void Action::prepare(){
  return;
}





