//
//yamlmodewrapper.h
// 
/***************************************************************************
 *   Created by James Barbetti on 17-May-2021                              *
 *   james_barbetti@yahoo.com                                              *
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

#pragma once
#ifndef yaml_model_wrapper_h
#define yaml_model_wrapper_h

#include "modelexpression.h"       //for ModelExpression::InterpretedExpression

#include "modeldna.h"              //for ModelDNA
#include "modeldnaerror.h"         //for ModelDNAError
#include "modelprotein.h"          //for ModelProtein
#include "modelcodon.h"            //for ModelCodon
#include "modelbin.h"              //for ModelBIN
#include "modelmorphology.h"       //for ModelMorphology
#include "modelmixture.h"          //for ModelMixture

#include "ratefree.h"              //for RateFree
#include "ratefreeinvar.h"         //for RateFreeInvar
#include "rateheterotachy.h"       //for RateHeterotachy
#include "rateheterotachyinvar.h"  //for RateHeterotachyInvar
#include "ratemeyerdiscrete.h"     //for RateMeyerDiscrete
#include "ratemeyerhaeseler.h"     //for RateMeyerHaeseler
#include "ratekategory.h"          //for RateKategory

#include "modelinfofromyamlfile.h" //for ModelInfoFromYAMLFile, etc.
#include <tree/phylotree.h>        //for PhyloTree

template <class S> class YAMLModelWrapper: public S {
protected:
    bool                   is_info_owned;
    ModelInfoFromYAMLFile* model_info;
    PhyloTree*             report_tree;
public:
    typedef S super;
    using   S::freq_type;
    using   S::num_params;
    using   S::num_states;
    using   S::rates;
    using   S::state_freq;
    
    using   S::afterVariablesChanged;
    using   S::getNDim;
    using   S::getNumberOfRates;
    using   S::setRateMatrix;

    YAMLModelWrapper() = delete;

    YAMLModelWrapper(ModelInfoFromYAMLFile& info,
                     bool  make_copy, 
                     PhyloTree* tree, PhyloTree* report_to_tree)
        : super(tree, report_to_tree)
        , is_info_owned(make_copy)
        , model_info(make_copy ? new ModelInfoFromYAMLFile(info) : &info)
        , report_tree(report_to_tree) {
        model_info->logVariablesTo(*report_to_tree);
    }

    ~YAMLModelWrapper() {
        if (is_info_owned) {
            delete model_info;
            model_info = nullptr;
        }
    }
    
    void acceptParameterList(std::string parameter_list) {
        //parameter_list is passed by value so it can be modified
        //(without those changes being copied back to the original)
        if (model_info->acceptParameterList(parameter_list, report_tree)) {
            setRateMatrixFromModel();
        }
    }
    
    virtual void setBounds(double *lower_bound, double *upper_bound,
                            bool *bound_check) {
        if (isMixtureModel()) {
            super::setBounds(lower_bound, upper_bound, bound_check);
            return;
        }
        int ndim = getNDim();
        for (int i = 1; i <= ndim; ++i) {
            lower_bound[i] = MIN_RATE;
            upper_bound[i] = MAX_RATE;
            bound_check[i] = false;
        }
        std::vector<ModelParameterType> types;
        types = { ModelParameterType::PROPORTION, 
                  ModelParameterType::INVARIANT_PROPORTION, 
                  ModelParameterType::RATE };
        model_info->setBounds(ndim, types, lower_bound,
                              upper_bound, bound_check);
    }

    virtual void afterVariablesChanged() {
        //Overridden in YAMLModelMixture
    }

    virtual bool getVariables(double *variables) {
        bool changed = false;
        if (isMixtureModel()) {
            changed = super::getVariables(variables);
            if (changed) {
                afterVariablesChanged();
            }
            return changed;
        }
        if (num_params > 0) {
            int num_all = getNumberOfRates();
            for (int i = 0; i < num_all; i++) {
                if (rates[i] != variables[i] ) {
                    TREE_LOG_LINE(*report_tree, VerboseMode::VB_MAX,
                                  " estimated rates[" << i << "] changing"
                                  " from " << rates[i] << " to " << variables[i]);
                    rates[i] = variables[i];
                    changed  = true;
                }
            }
        }
        int ndim = getNDim();
        int first_freq_index = (ndim-num_states+2);
        if (freq_type == StateFreqType::FREQ_ESTIMATE) {
            auto read_freq = variables+first_freq_index;
            for (int i=0; i<num_states-1; ++i) {
                if (state_freq[i]!=read_freq[i]) {
                    TREE_LOG_LINE(*report_tree, VerboseMode::VB_MAX,
                                  "  estimated freqs[" << i << "] changing"
                                  << " from " << state_freq[i]
                                  << " to " << read_freq[i]);
                    state_freq[i] = read_freq[i];
                    changed       = true;
                }
            }
            //Set the last frequency to the residual
            //(one minus the sum of the others)
            if (scaleStateFreq()) {
                changed = true;
                model_info->assignLastFrequency(state_freq[num_states-1]);
            }
        } else {
            changed |= freqsFromParams(state_freq, variables+num_params+1,
                                       freq_type);
        }
        if (changed) {
            TREE_LOG_LINE(*report_tree, VerboseMode::VB_MAX, "");
            model_info->updateVariables(variables, first_freq_index, ndim);
            model_info->logVariablesTo(*report_tree);
            setRateMatrixFromModel();
            afterVariablesChanged();
        }
        return changed;
    }

    virtual void afterWeightsChanged() {
    }
    
    virtual bool scaleStateFreq() {
        // make the frequencies sum to 1.0
        bool   changed = false;
        double sum     = 0.0;
        for (int i = 0; i < num_states-1; ++i) {
            sum += state_freq[i];
        }
        if (1.0<sum) {
            sum     += state_freq[num_states-1];
            changed  = true;
            for (int i = 0; i < num_states; ++i) {
                state_freq[i] /= sum;
            }
        } else {
            //Set last state frequency to 1.0 minus
            //the sum of the others
            double residual = 1.0 - sum;
            if (state_freq[num_states-1] != residual) {
                state_freq[num_states-1] = residual;
                changed = true;
            }
        }
        return changed;
    }

    virtual void setVariables(double *variables) {
        if (isMixtureModel()) {
            super::setVariables(variables);
            return;
        }
        if (num_params > 0) {
            for (int i = 0; i < num_params; ++i) {
                variables[i] = rates[i];
            }
        }
        if (freq_type == StateFreqType::FREQ_ESTIMATE) {
            // 2015-09-07: relax the sum of state_freq to be 1.0,
            // this will be done at the end of optimization
            int ndim = getNDim();
            memcpy(variables+(ndim-num_states+2), state_freq,
                   (num_states-1)*sizeof(double));
        } else {
            paramsFromFreqs(variables+num_params+1,
                            state_freq, freq_type);
        }
    }
    
    void setRateMatrixFromModel() {
        auto rank = model_info->getRateMatrixRank();
        ASSERT( rank == num_states );
        
        DoubleVector      rates;
        const char*       separator = "";
        std::stringstream trace;
        trace << "Rate Matrix: { ";
        
        model_info->forceAssign("num_states", num_states);
        ModelVariable& row_var    = model_info->forceAssign("row",    0);
        ModelVariable& column_var = model_info->forceAssign("column", 0);
        
        for (int row = 0; row < rank; ++row) {
            row_var.setValue(static_cast<double>(row+1));
            for (int col = 0; col < rank; ++col) {
                column_var.setValue(static_cast<double>(col+1));
                if (col != row) {
                    std::string expr_string =
                        model_info->getRateMatrixExpression(row,col);
                    typedef ModelExpression::InterpretedExpression Interpreter;
                    try {
                        Interpreter interpreter(*model_info, expr_string);
                        double entry = interpreter.evaluate();
                        rates.push_back(entry);
                        trace << separator << entry;
                    }
                    catch (ModelExpression::ModelException x) {
                        std::stringstream msg;
                        msg << "Error parsing expression"
                            << " for " << model_info->getName()
                            << " rate matrix entry"
                            << " for row "    << (row + 1) << ","
                            << " and column " << (col + 1) << ": "
                            << x.getMessage() << "\n";

                        msg << "Rate Matrix rank was: "
                            << model_info->getRateMatrixRank() << "\n"
                            << "Rate Matrix formula was: " 
                            << model_info->getRateMatrixFormula() << "\n"
                            << "Rate Matrix expressions were:\n";
                        separator = "";
                        for (int r=0; r<model_info->getRateMatrixRank(); ++r) {
                            for (int c=0; c<model_info->getRateMatrixRank(); ++c) {
                                msg << separator << model_info->getRateMatrixExpression(r, c);
                                separator = ",";
                            }
                            separator = "\n";
                        }
                        outError(msg.str());
                    }
                } else {
                    trace << separator << "-";
                }
                separator = ", ";
            }
        }
        trace << " }";
        TREE_LOG_LINE(*report_tree, VerboseMode::VB_MAX, trace.str());
        setRateMatrix(rates.data());
    }
    
    virtual void computeTipLikelihood(PML::StateType state, double *state_lk) {
        int state_num = static_cast<int>(state);
        if ( state_num < model_info->getTipLikelihoodMatrixRank()) {
            model_info->computeTipLikelihoodsForState(state, num_states, state_lk);
        } else if (state_num < num_states) {
            // single state
            memset(state_lk, 0, num_states*sizeof(double));
            state_lk[state] = 1.0;
        } else {
            // unknown state
            for (int i = 0; i < num_states; i++) {
                state_lk[i] = 1.0;
            }
        }
    }
    
    virtual void writeInfo(ostream &out) {
        model_info->writeInfo("Weight parameters    ", ModelParameterType::WEIGHT,     out);
        model_info->writeInfo("Proportion parameters", ModelParameterType::PROPORTION, out);
        model_info->writeInfo("Invariant proportion parameters",
                              ModelParameterType::INVARIANT_PROPORTION, out);
        model_info->writeInfo("Rate parameters      ", ModelParameterType::RATE,       out);
        model_info->writeInfo("State frequencies    ", ModelParameterType::FREQUENCY,  out);
    }

    virtual bool isMixtureModel() const {
        return false;
    }

    const ModelInfoFromYAMLFile* getModelInfo() const {
        return model_info;
    }

    ModelInfoFromYAMLFile* getModelInfo() {
        return model_info;
    }

	/**
		@return true if an ascertainment bias correction has been
		        specified for this model (if one was).  
	*/
	virtual bool getSpecifiedAscertainmentBiasCorrection(ASCType& asc_type) { 
        return model_info->checkAscertainmentBiasCorrection(false, asc_type);
    }

	/**
		@return a newly allocated Rate Model that was specified, for this
		        model (if one was).
	*/
	virtual RateHeterogeneity* getSpecifiedRateModel(PhyloTree* tree) { 
        return model_info->getSpecifiedRateModel(tree);
    }
};

class YAMLModelDNA: public YAMLModelWrapper<ModelDNA> {
public:
    typedef YAMLModelWrapper<ModelDNA> super;
    YAMLModelDNA(ModelInfoFromYAMLFile& info,
                 bool make_copy, const char *model_name, 
                 std::string model_params, StateFreqType freq, 
                 std::string freq_params,  PhyloTree*    tree, 
                 PhyloTree* report_to_tree);
};

class YAMLModelDNAError: public YAMLModelWrapper<ModelDNAError> {
public:
    typedef YAMLModelWrapper<ModelDNAError> super;
    YAMLModelDNAError(ModelInfoFromYAMLFile& info,
                      bool make_copy, const char *model_name, 
                      std::string model_params, StateFreqType freq, 
                      std::string freq_params,  PhyloTree*    tree, 
                      PhyloTree* report_to_tree);
    bool getVariables(double *variables);
};

class YAMLModelProtein: public YAMLModelWrapper<ModelProtein> {
public:
    typedef YAMLModelWrapper<ModelProtein> super;
    YAMLModelProtein(ModelInfoFromYAMLFile& info, 
                     bool make_copy, const char *model_name,
                     std::string model_params, StateFreqType freq, 
                     std::string freq_params,  ModelsBlock* block,
                     PhyloTree *tree, PhyloTree* report_to_tree);
};

class YAMLModelBinary: public YAMLModelWrapper<ModelBIN> {
public:
    typedef YAMLModelWrapper<ModelBIN> super;
    YAMLModelBinary(ModelInfoFromYAMLFile& info,
                    bool  make_copy, const char *model_name, 
                    std::string model_params, StateFreqType freq, 
                    std::string freq_params,  PhyloTree*    tree, 
                    PhyloTree* report_to_tree);
};

class YAMLModelMorphology: public YAMLModelWrapper<ModelMorphology> {
public:
    typedef YAMLModelWrapper<ModelMorphology> super;
    YAMLModelMorphology(ModelInfoFromYAMLFile& info,
                        bool make_copy, const char *model_name, 
                        std::string model_params, StateFreqType freq, 
                        std::string freq_params,  PhyloTree*    tree, 
                        PhyloTree* report_to_tree);
};

class YAMLModelCodon: public YAMLModelWrapper<ModelCodon> {
public:
    typedef YAMLModelWrapper<ModelCodon> super;
    YAMLModelCodon(ModelInfoFromYAMLFile& info, 
                   bool make_copy, const char *model_name, 
                   std::string model_params, StateFreqType freq, 
                   std::string freq_params,  PhyloTree*    tree, 
                   PhyloTree* report_to_tree);
};

class YAMLModelMixture: public YAMLModelWrapper<ModelMixture> {
protected:
    //Pointers to the information associated with each of the models
public:
    typedef YAMLModelWrapper<ModelMixture> super;
    YAMLModelMixture(ModelInfoFromYAMLFile& info, bool make_copy,              
                     const char* model_name, StateFreqType freq,
                     ModelsBlock* models_block, PhyloTree *tree, 
                     PhyloTree* report_to_tree);

    virtual bool isMixtureModel() const;
    virtual void setRateMatrixFromModel();
    virtual void afterVariablesChanged();
    virtual void afterWeightsChanged();
};

template <class R> class YAMLRateModelWrapper: public R {
protected:
    ModelInfoFromYAMLFile model_info;
    PhyloTree*            report_tree;
    int                   number_of_variable_shapes;
    int                   number_of_variable_proportions;
    int                   number_of_variable_rates;
    mutable int           number_of_variables;
    bool                  only_optimizing_rates;
public:
    typedef R super;
    using super::getNDim;
    using super::setFixGammaShape;
    using super::setFixProportions;
    using super::setFixRates;
    using super::isOptimizingProportions;
    using super::isOptimizingRates;
    using super::isOptimizingShapes;
    using super::startCheckpoint;
    using super::endCheckpoint;
    using super::checkpoint;
    using super::sortUpdatedRates;

    YAMLRateModelWrapper(const ModelInfoFromYAMLFile& info,
                     PhyloTree* tree)
        : super(info.getNumberOfRateCategories(), tree, tree)
        , model_info(info), report_tree(tree) {
        calculateNDim();
    }

    void calculateNDim() {
        number_of_variable_shapes      = model_info.getNumberOfVariableShapes();
        number_of_variable_proportions = model_info.getNumberOfVariableProportions();
        number_of_variable_rates       = model_info.getNumberOfVariableRates();
        number_of_variables   
            = (isOptimizingShapes()      ? number_of_variable_shapes      : 0)
            + (isOptimizingProportions() ? number_of_variable_proportions : 0)
            + (isOptimizingRates()       ? number_of_variable_rates       : 0);
        setFixGammaShape  ( number_of_variable_shapes==0 );
        setFixProportions ( number_of_variable_proportions==0 );
        setFixRates       ( number_of_variable_rates==0 );
    }

    void acceptParameterList(std::string parameter_list) {
        //parameter_list is passed by value so it can be modified
        if (model_info.acceptParameterList(parameter_list, report_tree)) {
            calculateNDim();
            //Todo: Look at what RateFree does when it accepts
            //command line parameters (in its main constructor)
        }
    }

    virtual int getNDim() const {
        number_of_variables   
            = (isOptimizingShapes()      ? number_of_variable_shapes      : 0)
            + (isOptimizingProportions() ? number_of_variable_proportions : 0)
            + (isOptimizingRates()       ? number_of_variable_rates       : 0);
        return number_of_variables;
    }

    virtual void setBounds(double* lower_bound, double* upper_bound,
                            bool*  bound_check) {
        int ndim = getNDim();
        std::vector<ModelParameterType> types;
        if (isOptimizingShapes()) {
            types.push_back(ModelParameterType::SHAPE);
        }
        if (isOptimizingProportions()) {
            types.push_back(ModelParameterType::PROPORTION);
            types.push_back(ModelParameterType::INVARIANT_PROPORTION);
        }
        if (isOptimizingProportions()) {
            types.push_back(ModelParameterType::RATE);
        }
        model_info.setBounds(ndim, types, lower_bound,
                             upper_bound, bound_check);
    }

    void setProportionToleranceFromModel() {
        if (0<model_info.getNumberOfProportions()) {
            YAMLFileParameter& param = model_info.getProportionParameter();
            if (!param.tolerance_expression.empty()) {
                param.tolerance = model_info.evaluateExpression(param.tolerance_expression, 
                                                        "proportion tolerance");
                super::setProportionTolerance(param.tolerance);
            }
        }
    }

    void setRateToleranceFromModel() {
        if (0<model_info.getNumberOfRateCategories()) {
            YAMLFileParameter& param = model_info.getRateParameter();
            if (!param.tolerance_expression.empty()) {
                param.tolerance = model_info.evaluateExpression
                                  (param.tolerance_expression, 
                                   "rate tolerance");
                super::setRateTolerance(param.tolerance);
            }
        }
    }

    virtual void updateRateClassFromModelVariables() = 0 ;

    virtual bool getVariables(double* variables) {
        int  index = 1;
        int  ndim  = getNDim();
        bool rc    = false;

        if (isOptimizingShapes()) {
            rc  |= model_info.updateModelVariablesByType(variables, ndim, false,
                                                         ModelParameterType::SHAPE,      index);
        }
        if (isOptimizingProportions()) {
            rc  |= model_info.updateModelVariablesByType(variables, ndim, false, 
                                                         ModelParameterType::PROPORTION, index);
            rc  |= model_info.updateModelVariablesByType(variables, ndim, false, 
                                                         ModelParameterType::INVARIANT_PROPORTION, 
                                                         index);
        }
        if (isOptimizingRates()) {
            rc  |= model_info.updateModelVariablesByType(variables, ndim, false, 
                                                         ModelParameterType::RATE,       index);
        }
        if (rc) {
            updateRateClassFromModelVariables();
        }
        return rc;
    }

    virtual void setVariables(double *variables) {
        int index = 1;
        int ndim  = getNDim();
        if (isOptimizingShapes()) {
            model_info.readModelVariablesByType(variables, ndim, false,
                                                ModelParameterType::SHAPE,      
                                                index);
        }
        if (isOptimizingProportions()) {
            model_info.readModelVariablesByType(variables, ndim, false,
                                                ModelParameterType::PROPORTION, 
                                                index);
            model_info.readModelVariablesByType(variables, ndim, false,
                                                ModelParameterType::INVARIANT_PROPORTION, 
                                                index);
        }
        if (isOptimizingRates()) {
            model_info.readModelVariablesByType(variables, ndim, false,
                                                ModelParameterType::RATE,       
                                                index);
        }
    }

    virtual void saveCheckpoint() {
        startCheckpoint();
        model_info.saveToCheckpoint(checkpoint);
        endCheckpoint();
    }

    virtual void restoreCheckpoint() {
        startCheckpoint();
        model_info.restoreFromCheckpoint(checkpoint);
        endCheckpoint();
    }

    virtual void writeInfo(ostream &out) {
        model_info.writeInfo("Shapes     ",           ModelParameterType::SHAPE,      out);
        model_info.writeInfo("Proportions",           ModelParameterType::PROPORTION, out);
        model_info.writeInfo("Invariant Proportions", ModelParameterType::INVARIANT_PROPORTION, out);
        model_info.writeInfo("Rates      ",           ModelParameterType::RATE,       out);
    }
};

class YAMLRateFree: public YAMLRateModelWrapper<RateFree> {
public:
    typedef YAMLRateModelWrapper<RateFree> super;
    YAMLRateFree(PhyloTree *tree, PhyloTree* report_to_tree,
                 ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();
};

class YAMLRateFreeInvar:public YAMLRateModelWrapper<RateFreeInvar> {
public:
    typedef YAMLRateModelWrapper<RateFreeInvar> super;
    YAMLRateFreeInvar(PhyloTree *tree, PhyloTree* report_to_tree,
                      ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();
};

class YAMLRateHeterotachy: public YAMLRateModelWrapper<RateHeterotachy> {
public:
    typedef YAMLRateModelWrapper<RateHeterotachy> super;
    YAMLRateHeterotachy(PhyloTree *tree, PhyloTree* report_to_tree,
                        ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();
};

class YAMLRateHeterotachyInvar:public YAMLRateModelWrapper<RateHeterotachyInvar> {
public:
    typedef YAMLRateModelWrapper<RateHeterotachyInvar> super;
    YAMLRateHeterotachyInvar(PhyloTree *tree, PhyloTree* report_to_tree,
                            ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();
};

class YAMLRateInvar:public YAMLRateModelWrapper<RateInvar> {
public:
    typedef YAMLRateModelWrapper<RateInvar> super;
    YAMLRateInvar(PhyloTree *tree, PhyloTree* report_to_tree,
                  ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();
};

class YAMLRateMeyerDiscrete:public YAMLRateModelWrapper<RateMeyerDiscrete> {
public:
    typedef YAMLRateModelWrapper<RateMeyerDiscrete> super;
    YAMLRateMeyerDiscrete(PhyloTree *tree, PhyloTree* report_to_tree,
                     ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();    
};

class YAMLRateMeyerHaeseler:public YAMLRateModelWrapper<RateMeyerHaeseler> {
public:
    typedef YAMLRateModelWrapper<RateMeyerHaeseler> super;
    YAMLRateMeyerHaeseler(PhyloTree *tree, PhyloTree* report_to_tree,
                     ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();    
};

class YAMLRateKategory:public YAMLRateModelWrapper<RateKategory> {
public:
    typedef YAMLRateModelWrapper<RateKategory> super;
    YAMLRateKategory(PhyloTree *tree, PhyloTree* report_to_tree,
                     ModelInfoFromYAMLFile& info);
    virtual void updateRateClassFromModelVariables();
    virtual void sortUpdatedRates();    
};

#endif //yaml_model_wrapper_h