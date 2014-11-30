/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2014 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "core/ActionRegister.h"
#include "core/PlumedMain.h"
#include "core/Atoms.h"
#include "tools/Units.h"
#include "tools/Pbc.h"
#include "ActionVolume.h"

//+PLUMEDOC MCOLVARF CAVITY
/*
This quantity can be used to calculate functions of the distribution of collective 
variables for the atoms that lie in a box defined by the positions of four atoms.

Each of the base quantities calculated by a multicolvar can can be assigned to a particular point in three 
dimensional space. For example, if we have the coordination numbers for all the atoms in the
system each coordination number can be assumed to lie on the position of the central atom. 
Because each base quantity can be assigned to a particular point in space we can calculate functions of the
distribution of base quantities in a particular part of the box by using:

\f[
\overline{s}_{\tau} = \frac{ \sum_i f(s_i) w(u_i,v_i,w_i) }{ \sum_i w(u_i,v_i,w_i) }  
\f]  

where the sum is over the collective variables, \f$s_i\f$, each of which can be thought to be at \f$ (u_i,v_i,z_i)\f$.
Notice that here (at variance with what is done in \ref AROUND) we have transformed from the usual \f$(x_i,y_i,z_i)\f$ 
position to a position in \f$ (u_i,v_i,z_i)\f$.  This is done using a rotation matrix as follows:

\f[
\left(
\begin{matrix}
 u_i \\
 v_i \\
 w_i
\end{matrix}
\right) = \mathbf{R}
\left(
\begin{matrix}
 x_i - x_o \\
 y_i - y_o \\
 z_i - z_o
\end{matrix}
\right)
\f]

where \f$\mathbf{R}\f$ is a rotation matrix that is calculated by constructing a set of three orthonormal vectors from the 
refererence positions specified by the user. The first of these unit vectors points from the first reference atom to the second.  
The second is then the normal to the plane containing atoms 1,2 and 3 and the the third is the unit vector orthogonal to 
these first two vectors.  \f$(x_o,y_o,z_o)\f$, meanwhile, specifies the position of the first reference atom.  

In the previous function \f$ w(u_i,v_i,w_i) \f$ measures whether or not the system is in the subregion of interest. It
is equal to:

\f[
w(u_i,v_i,w_i) = \int_{0}^{u'} \int_{0}^{v'} \int_{0}^{w'} \textrm{d}u\textrm{d}v\textrm{d}w 
   K\left( \frac{u - u_i}{\sigma} \right)K\left( \frac{v - v_i}{\sigma} \right)K\left( \frac{w - w_i}{\sigma} \right) 
\f]

where \f$K\f$ is one of the kernel functions described on \ref histogrambead and \f$\sigma\f$ is a bandwidth parameter.
The function \f$(s_i)\f$ can be any of the usual LESS_THAN, MORE_THAN, WITHIN etc that are used in all other multicolvars. 
The vector connecting atom 1 to atom 4 is used to define the extent of the box in each of the \f$u\f$, \f$v\f$ and \f$w\f$ 
directions.  Essentially the vector connecting atom 1 to atom 4 is projected onto the three unit vectors
described above and the resulting projections determine the \f$u'\f$, \f$v'\f$ and \f$w'\f$ parameters in the above expression. 

\par Examples

The following commands tell plumed to calculate the number of atoms in an ion chanel in a protein.
The extent of the chanel is calculated from the positions of atoms 1, 4, 5 and 11. The final value will be labeled cav.

\verbatim
d1: DENSITY SPECIES=20-500 
CAVITY DATA=d1 ATOMS=1,4,5,11 SIGMA=0.1 LABEL=cav
\endverbatim

The following command tells plumed to calculate the coordination numbers (with other water molecules) for the water 
molecules in the protein channel described above.  The average coordination number and the number of coordination
numbers more than 4 is then calculated.  The values of these two quantities are given the labels cav.mean and cav.more-than

\verbatim
d1: COORDINATIONNUMBER SPECIES=20-500
CAVITY DATA=d1 ATOMS=1,4,5,11 SIGMA=0.1 MEAN MORE_THAN={RATIONAL R_0=4} LABEL=cav
\endverbatim

*/
//+ENDPLUMEDOC

namespace PLMD {
namespace multicolvar {

class VolumeCavity : public ActionVolume {
private:
  bool boxout;
  OFile boxfile;
  double lenunit;
  double jacob_det;
  double len_bi, len_cross, len_perp, sigma;
  Vector origin, bi, cross, perp;
  std::vector<Vector> dlbi, dlcross, dlperp;
  std::vector<Tensor> dbi, dcross, dperp;
public:
  static void registerKeywords( Keywords& keys );
  VolumeCavity(const ActionOptions& ao);
  ~VolumeCavity();
  void setupRegions();
  void update();
  double calculateNumberInside( const Vector& cpos, Vector& derivatives, Tensor& vir, std::vector<Vector>& refders ) const ;
};

PLUMED_REGISTER_ACTION(VolumeCavity,"CAVITY")

void VolumeCavity::registerKeywords( Keywords& keys ){
  ActionVolume::registerKeywords( keys );
  keys.add("atoms","ATOMS","the positions of four atoms that define spatial extent of the cavity");
  keys.addFlag("PRINT_BOX",false,"write out the positions of the corners of the box to an xyz file");
  keys.add("optional","FILE","the file on which to write out the box coordinates");
  keys.add("optional","UNITS","( default=nm ) the units in which to write out the corners of the box");
}

VolumeCavity::VolumeCavity(const ActionOptions& ao):
Action(ao),
ActionVolume(ao),
boxout(false),
lenunit(1.0),
dlbi(4),
dlcross(4),
dlperp(4),
dbi(3),
dcross(3),
dperp(3)
{
  std::vector<AtomNumber> atoms;
  parseAtomList("ATOMS",atoms);
  if( atoms.size()!=4 ) error("number of atoms should be equal to four");

  log.printf("  boundaries for region are calculated based on positions of atoms : ");
  for(unsigned i=0;i<atoms.size();++i) log.printf("%d ",atoms[i].serial() );
  log.printf("\n");

  boxout=false; parseFlag("PRINT_BOX",boxout);
  if(boxout){
     std::string boxfname; parse("FILE",boxfname);
     if(boxfname.length()==0) error("no name for box file specified");
     std::string unitname; parse("UNITS",unitname);
     if ( unitname.length()>0 ){
        Units u; u.setLength(unitname); 
        lenunit=plumed.getAtoms().getUnits().getLength()/u.getLength();
     } else {
        unitname="nm";
     }
     boxfile.link(*this);
     boxfile.open( boxfname.c_str() );
     log.printf("  printing box coordinates on file named %s in %s \n",boxfname.c_str(), unitname.c_str() );
  }  
  
  checkRead();
  requestAtoms(atoms);
  // We have to readd the dependency because requestAtoms removes it
  addDependency( getPntrToMultiColvar() );  
}

VolumeCavity::~VolumeCavity(){
}

void VolumeCavity::setupRegions(){
  // Make some space for things
  Vector d1, d2, d3;
  
  // Retrieve the sigma value
  sigma=getSigma();
  // Set the position of the origin
  origin=getPosition(0);

  // Get two vectors
  d1 = pbcDistance(origin,getPosition(1));
  double d1l=d1.modulo();
  d2 = pbcDistance(origin,getPosition(2));
  double dl2=d2.modulo();

  // Find the vector connecting the origin to the top corner of 
  // the subregion
  d3 = pbcDistance(origin,getPosition(3));

  double bif, crossf, perpf; bif=crossf=perpf=1.0;
  // Create a set of unit vectors
  bi = d1 / d1l; len_bi=dotProduct( d3, bi );
  if( len_bi<0 ){ bi=-bi; len_bi=-len_bi; bif=-1.;}
  cross = crossProduct( bi, d2/dl2 ); len_cross=dotProduct( d3, cross );
  if( len_cross<0 ){ cross=-cross; len_cross=-len_cross; crossf=-1; }
  perp = crossProduct( cross, bi ); len_perp=dotProduct( d3, perp );
  if( len_perp<0 ){ perp=-perp; len_perp=-len_perp; perpf=-1; }
  if( len_bi<=0 || len_cross<=0 || len_bi<=0 ) plumed_merror("Invalid box coordinates");

  // Calculate derivatives of box shape with respect to atoms
  double d1l3=d1l*d1l*d1l; 
  dbi[0](0,0) = bif*( -(d1[1]*d1[1]+d1[2]*d1[2])/d1l3 );   // dx/dx
  dbi[0](0,1) = bif*(  d1[0]*d1[1]/d1l3 );                 // dx/dy
  dbi[0](0,2) = bif*(  d1[0]*d1[2]/d1l3 );                 // dx/dz 
  dbi[0](1,0) = bif*(  d1[1]*d1[0]/d1l3 );                 // dy/dx
  dbi[0](1,1) = bif*( -(d1[0]*d1[0]+d1[2]*d1[2])/d1l3 );   // dy/dy
  dbi[0](1,2) = bif*(  d1[1]*d1[2]/d1l3 );
  dbi[0](2,0) = bif*(  d1[2]*d1[0]/d1l3 );
  dbi[0](2,1) = bif*(  d1[2]*d1[1]/d1l3 );
  dbi[0](2,2) = bif*( -(d1[1]*d1[1]+d1[0]*d1[0])/d1l3 );

  dbi[1](0,0) = bif*( (d1[1]*d1[1]+d1[2]*d1[2])/d1l3 );
  dbi[1](0,1) = bif*( -d1[0]*d1[1]/d1l3 );
  dbi[1](0,2) = bif*( -d1[0]*d1[2]/d1l3 );
  dbi[1](1,0) = bif*( -d1[1]*d1[0]/d1l3 );
  dbi[1](1,1) = bif*( (d1[0]*d1[0]+d1[2]*d1[2])/d1l3 );
  dbi[1](1,2) = bif*( -d1[1]*d1[2]/d1l3 );
  dbi[1](2,0) = bif*( -d1[2]*d1[0]/d1l3 );
  dbi[1](2,1) = bif*( -d1[2]*d1[1]/d1l3 );
  dbi[1](2,2) = bif*( (d1[1]*d1[1]+d1[0]*d1[0])/d1l3 );
  dbi[2].zero();

  std::vector<Tensor> dd2(3); double d123=dl2*dl2*dl2;
  dd2[0](0,0) = ( -(d2[1]*d2[1]+d2[2]*d2[2])/d123 );
  dd2[0](0,1) = (  d2[0]*d2[1]/d123 );
  dd2[0](0,2) = (  d2[0]*d2[2]/d123 );
  dd2[0](1,0) = (  d2[1]*d2[0]/d123 );
  dd2[0](1,1) = ( -(d2[0]*d2[0]+d2[2]*d2[2])/d123 );
  dd2[0](1,2) = (  d2[1]*d2[2]/d123 );
  dd2[0](2,0) = (  d2[2]*d2[0]/d123 );
  dd2[0](2,1) = (  d2[2]*d2[1]/d123 );
  dd2[0](2,2) = ( -(d2[1]*d2[1]+d2[0]*d2[0])/d123 );

  dd2[1].zero();
  dd2[2](0,0) = ( (d2[1]*d2[1]+d2[2]*d2[2])/d123 );
  dd2[2](0,1) = ( -d2[0]*d2[1]/d123 );
  dd2[2](0,2) = ( -d2[0]*d2[2]/d123 );
  dd2[2](1,0) = ( -d2[1]*d2[0]/d123 );
  dd2[2](1,1) = ( (d2[0]*d2[0]+d2[2]*d2[2])/d123 );
  dd2[2](1,2) = ( -d2[1]*d2[2]/d123 );
  dd2[2](2,0) = ( -d2[2]*d2[0]/d123 );
  dd2[2](2,1) = ( -d2[2]*d2[1]/d123 );
  dd2[2](2,2) = ( (d2[1]*d2[1]+d2[0]*d2[0])/d123 );
  d2 *= 1.0 / dl2;

  dcross[0].setCol( 0, crossf*( crossProduct( bi, dd2[0].getCol(0) ) + crossProduct( dbi[0].getCol(0), d2 ) ) );
  dcross[0].setCol( 1, crossf*( crossProduct( bi, dd2[0].getCol(1) ) + crossProduct( dbi[0].getCol(1), d2 ) ) );
  dcross[0].setCol( 2, crossf*( crossProduct( bi, dd2[0].getCol(2) ) + crossProduct( dbi[0].getCol(2), d2 ) ) );

  dcross[1].setCol( 0, crossf*( crossProduct( dbi[1].getCol(0), d2 ) ) );
  dcross[1].setCol( 1, crossf*( crossProduct( dbi[1].getCol(1), d2 ) ) );
  dcross[1].setCol( 2, crossf*( crossProduct( dbi[1].getCol(2), d2 ) ) );

  dcross[2].setCol( 0, crossf*( crossProduct( bi, dd2[2].getCol(0) ) ) );
  dcross[2].setCol( 1, crossf*( crossProduct( bi, dd2[2].getCol(1) ) ) );
  dcross[2].setCol( 2, crossf*( crossProduct( bi, dd2[2].getCol(2) ) ) );

  dperp[0].setCol( 0, perpf*( crossProduct( dcross[0].getCol(0), bi ) + crossProduct( cross, dbi[0].getCol(0) ) ) );
  dperp[0].setCol( 1, perpf*( crossProduct( dcross[0].getCol(1), bi ) + crossProduct( cross, dbi[0].getCol(1) ) ) );
  dperp[0].setCol( 2, perpf*( crossProduct( dcross[0].getCol(2), bi ) + crossProduct( cross, dbi[0].getCol(2) ) ) );

  dperp[1].setCol( 0, perpf*( crossProduct( dcross[1].getCol(0), bi ) + crossProduct( cross, dbi[1].getCol(0) ) ) );
  dperp[1].setCol( 1, perpf*( crossProduct( dcross[1].getCol(1), bi ) + crossProduct( cross, dbi[1].getCol(1) ) ) );
  dperp[1].setCol( 2, perpf*( crossProduct( dcross[1].getCol(2), bi ) + crossProduct( cross, dbi[1].getCol(2) ) ) );

  dperp[2].setCol( 0, perpf*( crossProduct( dcross[2].getCol(0), bi ) ) );
  dperp[2].setCol( 1, perpf*( crossProduct( dcross[2].getCol(1), bi ) ) );
  dperp[2].setCol( 2, perpf*( crossProduct( dcross[2].getCol(2), bi ) ) );

  // Now derivatives of lengths
  Tensor dd3( Tensor::identity() ); 
  dlbi[0] = matmul(d3,dbi[0]) - matmul(bi,dd3);  
  dlbi[1] = matmul(d3,dbi[1]);
  dlbi[2] = matmul(d3,dbi[2]);
  dlbi[3] = matmul(bi,dd3);

  dlcross[0] = matmul(d3,dcross[0]) - matmul(cross,dd3);  
  dlcross[1] = matmul(d3,dcross[1]);
  dlcross[2] = matmul(d3,dcross[2]);
  dlcross[3] = matmul(cross,dd3);

  dlperp[0] = matmul(d3,dperp[0]) - matmul(perp,dd3);  
  dlperp[1] = matmul(d3,dperp[1]);
  dlperp[2] = matmul(d3,dperp[2]);
  dlperp[3] = matmul(perp,dd3);

  // Need to calculate the jacobian
  Tensor jacob;
  jacob(0,0)=bi[0]; jacob(1,0)=bi[1]; jacob(2,0)=bi[2];
  jacob(0,1)=cross[0]; jacob(1,1)=cross[1]; jacob(2,1)=cross[2];
  jacob(0,2)=perp[0]; jacob(1,2)=perp[1]; jacob(2,2)=perp[2];
  jacob_det = fabs( jacob.determinant() );
}

void VolumeCavity::update(){
  if(boxout){ 
     boxfile.printf("%d\n",8);
     const Tensor & t(getPbc().getBox());
     if(getPbc().isOrthorombic()){
       boxfile.printf(" %f %f %f\n",lenunit*t(0,0),lenunit*t(1,1),lenunit*t(2,2));
     }else{
       boxfile.printf(" %f %f %f %f %f %f %f %f %f\n",
                    lenunit*t(0,0),lenunit*t(0,1),lenunit*t(0,2),
                    lenunit*t(1,0),lenunit*t(1,1),lenunit*t(1,2),
                    lenunit*t(2,0),lenunit*t(2,1),lenunit*t(2,2)
              );
     }
     boxfile.printf("AR %f %f %f \n",lenunit*origin[0],lenunit*origin[1],lenunit*origin[2]);
     Vector ut, vt, wt; 
     ut = origin + len_bi*bi; 
     vt = origin + len_cross*cross;
     wt = origin + len_perp*perp;
     boxfile.printf("AR %f %f %f \n",lenunit*(ut[0]) , lenunit*(ut[1]), lenunit*(ut[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(vt[0]) , lenunit*(vt[1]), lenunit*(vt[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(wt[0]) , lenunit*(wt[1]), lenunit*(wt[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(vt[0]+len_bi*bi[0]), 
                                      lenunit*(vt[1]+len_bi*bi[1]), 
                                      lenunit*(vt[2]+len_bi*bi[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(ut[0]+len_perp*perp[0]), 
                                      lenunit*(ut[1]+len_perp*perp[1]), 
                                      lenunit*(ut[2]+len_perp*perp[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(vt[0]+len_perp*perp[0]), 
                                      lenunit*(vt[1]+len_perp*perp[1]), 
                                      lenunit*(vt[2]+len_perp*perp[2]) );
     boxfile.printf("AR %f %f %f \n",lenunit*(vt[0]+len_perp*perp[0]+len_bi*bi[0]), 
                                      lenunit*(vt[1]+len_perp*perp[1]+len_bi*bi[1]), 
                                      lenunit*(vt[2]+len_perp*perp[2]+len_bi*bi[2]) );
  }
}

double VolumeCavity::calculateNumberInside( const Vector& cpos, Vector& derivatives, Tensor& vir, std::vector<Vector>& rderiv ) const {
  // Setup the histogram bead
  HistogramBead bead; bead.isNotPeriodic(); bead.setKernelType( getKernelType() );

  // Calculate distance of atom from origin of new coordinate frame
  Vector datom=pbcDistance( origin, cpos );
  double ucontr, uder, vcontr, vder, wcontr, wder;

  // Calculate contribution from integral along bi
  bead.set( 0, len_bi, sigma );
  double upos=dotProduct( datom, bi );
  ucontr=bead.calculate( upos, uder );
  double udlen=bead.uboundDerivative( upos ); 
  double uder2 = bead.lboundDerivative( upos ) - udlen; 

  // Calculate contribution from integral along cross
  bead.set( 0, len_cross, sigma );
  double vpos=dotProduct( datom, cross );
  vcontr=bead.calculate( vpos, vder );
  double vdlen=bead.uboundDerivative( vpos );
  double vder2 = bead.lboundDerivative( vpos ) - vdlen;

  // Calculate contribution from integral along perp
  bead.set( 0, len_perp, sigma );
  double wpos=dotProduct( datom, perp );
  wcontr=bead.calculate( wpos, wder );
  double wdlen=bead.uboundDerivative( wpos );
  double wder2 = bead.lboundDerivative( wpos ) - wdlen;

  Vector dfd; dfd[0]=uder*vcontr*wcontr; dfd[1]=ucontr*vder*wcontr; dfd[2]=ucontr*vcontr*wder;
  derivatives[0] = (dfd[0]*bi[0]+dfd[1]*cross[0]+dfd[2]*perp[0]);
  derivatives[1] = (dfd[0]*bi[1]+dfd[1]*cross[1]+dfd[2]*perp[1]);
  derivatives[2] = (dfd[0]*bi[2]+dfd[1]*cross[2]+dfd[2]*perp[2]);
  double tot = ucontr*vcontr*wcontr*jacob_det; 

  // Add reference atom derivatives
  dfd[0]=uder2*vcontr*wcontr; dfd[1]=ucontr*vder2*wcontr; dfd[2]=ucontr*vcontr*wder2;
  Vector dfld; dfld[0]=udlen*vcontr*wcontr; dfld[1]=ucontr*vdlen*wcontr; dfld[2]=ucontr*vcontr*wdlen;
  rderiv[0] = dfd[0]*matmul(datom,dbi[0]) + dfd[1]*matmul(datom,dcross[0]) + dfd[2]*matmul(datom,dperp[0]) +
              dfld[0]*dlbi[0] + dfld[1]*dlcross[0] + dfld[2]*dlperp[0] - derivatives; 
  rderiv[1] = dfd[0]*matmul(datom,dbi[1]) + dfd[1]*matmul(datom,dcross[1]) + dfd[2]*matmul(datom,dperp[1]) + 
              dfld[0]*dlbi[1] + dfld[1]*dlcross[1] + dfld[2]*dlperp[1];
  rderiv[2] = dfd[0]*matmul(datom,dbi[2]) + dfd[1]*matmul(datom,dcross[2]) + dfd[2]*matmul(datom,dperp[2]) + 
              dfld[0]*dlbi[2] + dfld[1]*dlcross[2] + dfld[2]*dlperp[2];
  rderiv[3] = dfld[0]*dlbi[3] + dfld[1]*dlcross[3] + dfld[2]*dlperp[3];

  vir.zero(); vir-=Tensor( cpos,derivatives );
  for(unsigned i=0;i<4;++i){
     vir -= Tensor( getPosition(i), rderiv[i] ); 
  }
 
  return tot;
}

}
}