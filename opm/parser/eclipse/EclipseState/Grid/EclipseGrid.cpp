/*
  Copyright 2014 Statoil ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/



#include <opm/parser/eclipse/Deck/Section.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>

#include <ert/ecl/ecl_grid.h>
namespace Opm {

    
    EclipseGrid::EclipseGrid(std::shared_ptr<const RUNSPECSection> runspecSection, std::shared_ptr<const GRIDSection> gridSection) {
        if (runspecSection->hasKeyword("DIMENS")) {
            DeckKeywordConstPtr dimens = runspecSection->getKeyword("DIMENS");
            DeckRecordConstPtr record = dimens->getRecord(0);
            std::vector<int> dims = {record->getItem("NX")->getInt(0) , 
                                     record->getItem("NY")->getInt(0) , 
                                     record->getItem("NZ")->getInt(0) };

            if (hasCornerPointKeywords(gridSection)) {
                
            } else if (hasCartesianKeywords(gridSection)) {
                initCartesianGrid(dims , gridSection);
            } else
                throw std::invalid_argument("The GRID section must have COORD / ZCORN or D?? keywords");
            
        } else
            throw std::invalid_argument("The RUNSPEC section must have the DIMENS keyword with grid dimensions");
    }


    int EclipseGrid::getNX( ) const {
        return ecl_grid_get_nx( m_grid.get() );
    }

    int EclipseGrid::getNY( ) const {
        return ecl_grid_get_ny( m_grid.get() );
    }

    int EclipseGrid::getNZ( ) const {
        return ecl_grid_get_nz( m_grid.get() );
    }

    
    bool EclipseGrid::hasCornerPointKeywords(std::shared_ptr<const GRIDSection> gridSection) {
        if (gridSection->hasKeyword("ZCORN") && gridSection->hasKeyword("COORD"))
            return true;
        else
            return false;
    }



    bool EclipseGrid::hasCartesianKeywords(std::shared_ptr<const GRIDSection> gridSection) {
        if ((gridSection->hasKeyword("DX") || gridSection->hasKeyword("DXV")) &&
            (gridSection->hasKeyword("DY") || gridSection->hasKeyword("DYV")) &&
            (gridSection->hasKeyword("DZ") || gridSection->hasKeyword("DZV")) &&
            gridSection->hasKeyword("TOPS"))
            return true;
        else
            return false;
    }


    void EclipseGrid::initCartesianGrid(const std::vector<int>& dims , std::shared_ptr<const GRIDSection> gridSection) {
        std::vector<double> DX = createDVector( dims , 0 , "DX" , "DXV" , gridSection);
        std::vector<double> DY = createDVector( dims , 1 , "DY" , "DYV" , gridSection);
        std::vector<double> DZ = createDVector( dims , 2 , "DZ" , "DZV" , gridSection);
        std::vector<double> TOPS = createTOPSVector( dims , DZ , gridSection );
        
        m_grid.reset( ecl_grid_alloc_dx_dy_dz_tops( dims[0] , dims[1] , dims[2] , DX.data() , DY.data() , DZ.data() , TOPS.data() , NULL ) , ecl_grid_free);
    }
    

    
    std::vector<double> EclipseGrid::createTOPSVector(const std::vector<int>& dims , const std::vector<double>& DZ , std::shared_ptr<const GRIDSection> gridSection) {
        size_t volume = dims[0] * dims[1] * dims[2];
        size_t area = dims[0] * dims[1];
        DeckKeywordConstPtr TOPSKeyWord = gridSection->getKeyword("TOPS");
        std::vector<double> TOPS = TOPSKeyWord->getSIDoubleData();
        
        if (TOPS.size() >= area) {
            TOPS.resize( volume );
            for (size_t targetIndex = TOPS.size(); targetIndex < volume; targetIndex++) {
                size_t sourceIndex = targetIndex - area;
                TOPS[targetIndex] = TOPS[sourceIndex] + DZ[sourceIndex];
            }
        }
        
        if (TOPS.size() != volume)
            throw std::invalid_argument("TOPS size mismatch");

        return TOPS;
    }

    
    
    std::vector<double> EclipseGrid::createDVector(const std::vector<int>& dims , size_t dim , const std::string& DKey , const std::string& DVKey, std::shared_ptr<const GRIDSection> gridSection) {
        size_t volume = dims[0] * dims[1] * dims[2];
        size_t area = dims[0] * dims[1];
        std::vector<double> D;
        if (gridSection->hasKeyword(DKey)) {
            DeckKeywordConstPtr DKeyWord = gridSection->getKeyword(DKey);
            D = DKeyWord->getSIDoubleData();
            

            if (D.size() >= area && DKey == "DZ") {
                /*
                  Special casing of the DZ keyword where you can choose to
                  only specify the top layer.
                */
                D.resize( volume );
                for (size_t targetIndex = D.size(); targetIndex < volume; targetIndex++) {
                    size_t sourceIndex = targetIndex - area;
                    D[targetIndex] = D[sourceIndex];
                }
            }
                
            if (D.size() != volume)
                throw std::invalid_argument(DKey + " size mismatch");
        } else {
            DeckKeywordConstPtr DVKeyWord = gridSection->getKeyword(DVKey);
            const std::vector<double>& DV = DVKeyWord->getSIDoubleData();
            if (DV.size() != (size_t) dims[0])
                throw std::invalid_argument(DVKey + " size mismatch");
            D.resize( volume );
            scatterDim( dims , 0 , DV , D );
        }
        return D;
    }
    
    
    void EclipseGrid::scatterDim(const std::vector<int>& dims , size_t dim , const std::vector<double>& DV , std::vector<double>& D) {
        int index[3];
        for (index[2] = 0;  index[2] < dims[2]; index[2]++) {
            for (index[1] = 0; index[1] < dims[1]; index[1]++) {
                for (index[0] = 0;  index[0] < dims[0]; index[0]++) {
                    size_t globalIndex = index[2] * dims[1] * dims[0] + index[1] * dims[0] + index[0];
                    D[globalIndex] = DV[ index[dim] ];
                }
            }
        }
    } 
    
}


