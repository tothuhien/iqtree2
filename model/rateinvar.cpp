/***************************************************************************
 *   Copyright (C) 2009 by BUI Quang Minh   *
 *   minh.bui@univie.ac.at   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "rateinvar.h"

void RateInvar::defaultInvariantProportion(double p_invar_requested) {
	if (0!=p_invar_requested) {
		p_invar = p_invar_requested;
	}
	else if (phylo_tree!=nullptr) {
		p_invar = phylo_tree->aln->frac_const_sites;
		if (p_invar!=0) {
            p_invar = max(p_invar*0.5, MIN_PINVAR);
		}
	} else {
		p_invar = MIN_PINVAR;
	}
	fix_p_invar = false;
	if (0 < p_invar) {
		// true unless -optfromgiven cmd line option
		fix_p_invar = !(Params::getInstance().optimize_from_given_params);
	}
	minimum   = MIN_PINVAR;
	maximum   = phylo_tree->aln->frac_const_sites;
	tolerance = TOL_PINVAR;
}

RateInvar::RateInvar(int dummy_categories, PhyloTree* tree,
                     PhyloTree* report_to_tree): super() {
	phylo_tree = tree;
	defaultInvariantProportion(0);
	name      = "+I";
	full_name = "Invar";
}

RateInvar::RateInvar(double p_invar_sites, PhyloTree *tree)
 : super()
{
	phylo_tree = tree;
	defaultInvariantProportion(p_invar_sites);
	name      = "+I";
	full_name = "Invar";
}

void RateInvar::startCheckpoint() {
    checkpoint->startStruct("RateInvar");
}

void RateInvar::saveCheckpoint() {
    startCheckpoint();
    CKP_SAVE(p_invar);
//    CKP_SAVE(fix_p_invar);
//    CKP_SAVE(optimize_p_invar);
    endCheckpoint();
    super::saveCheckpoint();
}

void RateInvar::restoreCheckpoint() {
    super::restoreCheckpoint();
    startCheckpoint();
    CKP_RESTORE(p_invar);
//    CKP_RESTORE(fix_p_invar);
//    CKP_RESTORE(optimize_p_invar);
    endCheckpoint();
}

std::string RateInvar::getNameParams() const {
	ostringstream str;
	str << "+I{" << p_invar << '}';
	return str.str();
}

double RateInvar::computeFunction(double p_invar_value) {
	p_invar = p_invar_value;
	phylo_tree->clearAllPartialLH();
	return -phylo_tree->computeLikelihood();
}

double RateInvar::targetFunk(double x[]) {
	getVariables(x);
	// fix bug: computeTip... will update ptn_invar vector
	phylo_tree->computePtnInvar();
	return -phylo_tree->computeLikelihood();
}

void RateInvar::setBounds(double *lower_bound, double *upper_bound,
                          bool *bound_check) {
	if (getNDim() == 0) {
		return;
	}
	lower_bound[1] = minimum;
	upper_bound[1] = maximum;
	bound_check[1] = true;
}

double RateInvar::optimizeParameters(double gradient_epsilon,
                                     PhyloTree* report_to_tree) {
								
    if (phylo_tree->aln->frac_const_sites == 0.0) {
        return -computeFunction(0.0);
    }
    if (fix_p_invar) {
        return -computeFunction(p_invar);
    }

    TREE_LOG_LINE(*phylo_tree, VerboseMode::VB_MAX, 
		          "Optimizing proportion of invariable sites...");
    
	double dummy[2] = { -1, 0 };
    setVariables(dummy);

    double negative_lh;
    double ferror;
    double step      = max(gradient_epsilon, tolerance);
    p_invar          = minimizeOneDimen(minimum, p_invar, maximum, step,
                                        &negative_lh, &ferror);
	dummy[1] = p_invar;
	getVariables(dummy);
    return -computeFunction(p_invar);
}

void RateInvar::writeInfo(ostream &out) {
	out << "Proportion of invariable sites: " << p_invar << endl;
}

void RateInvar::writeParameters(ostream &out) {
	out << "\t" << p_invar;
}

void RateInvar::setVariables(double *variables) {
	if (getNDim() == 0) {
		return;
	}
	variables[1] = p_invar;
}

bool RateInvar::getVariables(const double *variables) {
	if (getNDim() == 0) {
		return false;
	}
    bool changed = (p_invar != variables[1]);
	p_invar = variables[1];
    return changed;
}

bool   RateInvar::isOptimizingProportions() const       { return !fix_p_invar;  }
bool   RateInvar::isOptimizingRates()       const       { return false;         }
bool   RateInvar::isOptimizingShapes()      const       { return false;         }
bool   RateInvar::areProportionsFixed()     const       { return fix_p_invar;   }
double RateInvar::getMinimumProportion()    const       { return minimum;       }

void RateInvar::sortUpdatedRates      ()                { } //No rates, Nothing to do!
void RateInvar::setFixProportions     (bool fixed)      { fix_p_invar = fixed;  }
void RateInvar::setFixRates           (bool fixed)      { } //One rate, always zero!
void RateInvar::setMaximumProportion  (double max_prop) { maximum   = max_prop; }
void RateInvar::setMinimumProportion  (double min_prop) { minimum   = min_prop; }
void RateInvar::setProportionTolerance(double tol)      {
	 ASSERT(0<tol);
	 tolerance = tol;      
}

