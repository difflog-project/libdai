/*  Copyright (C) 2008  Frederik Eaton  [frederik at ofb dot net],
                        Joris Mooij  [joris dot mooij at tuebingen dot mpg dot de]

    This file is part of libDAI.

    libDAI is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    libDAI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libDAI; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <dai/gibbs.h>
#include <dai/util.h>
#include <dai/properties.h>


namespace dai {


using namespace std;


const char *Gibbs::Name = "GIBBS";


void Gibbs::setProperties( const PropertySet &opts ) {
    assert( opts.hasKey("iters") );
    props.iters = opts.getStringAs<size_t>("iters");

    if( opts.hasKey("verbose") )
        props.verbose = opts.getStringAs<size_t>("verbose");
    else
        props.verbose = 0;
}


PropertySet Gibbs::getProperties() const {
    PropertySet opts;
    opts.Set( "iters", props.iters );
    opts.Set( "verbose", props.verbose );
    return opts;
}


string Gibbs::printProperties() const {
    stringstream s( stringstream::out );
    s << "[";
    s << "iters=" << props.iters << ",";
    s << "verbose=" << props.verbose << "]";
    return s.str();
}


void Gibbs::construct() {
    _var_counts.clear();
    _var_counts.reserve( nrVars() );
    for( size_t i = 0; i < nrVars(); i++ )
        _var_counts.push_back( _count_t( var(i).states(), 0 ) );
    
    _factor_counts.clear();
    _factor_counts.reserve( nrFactors() );
    for( size_t I = 0; I < nrFactors(); I++ )
        _factor_counts.push_back( _count_t( factor(I).states(), 0 ) );

    _sample_count = 0;

    _state.clear();
    _state.resize( nrVars(), 0 );
}


void Gibbs::updateCounts() {
    for( size_t i = 0; i < nrVars(); i++ )
        _var_counts[i][_state[i]]++;
    for( size_t I = 0; I < nrFactors(); I++ )
        _factor_counts[I][getFactorEntry(I)]++;
    _sample_count++;
}


inline size_t Gibbs::getFactorEntry( size_t I ) {
    size_t f_entry = 0;
    for( int _j = nbF(I).size() - 1; _j >= 0; _j-- ) {
        // note that iterating over nbF(I) yields the same ordering
        // of variables as iterating over factor(I).vars()
        size_t j = nbF(I)[_j];
        f_entry *= var(j).states();
        f_entry += _state[j];
    }
    return f_entry;
}


inline size_t Gibbs::getFactorEntryDiff( size_t I, size_t i ) {
    size_t skip = 1;
    for( size_t _j = 0; _j < nbF(I).size(); _j++ ) {
        // note that iterating over nbF(I) yields the same ordering
        // of variables as iterating over factor(I).vars()
        size_t j = nbF(I)[_j];
        if( i == j )
            break;
        else
            skip *= var(j).states();
    }
    return skip;
}


Prob Gibbs::getVarDist( size_t i ) {
    assert( i < nrVars() );
    size_t i_states = var(i).states();
    Prob i_given_MB( i_states, 1.0 );

    // use markov blanket of var(i) to calculate distribution
    foreach( const Neighbor &I, nbV(i) ) {
        const Factor &f_I = factor(I);
        size_t I_skip = getFactorEntryDiff( I, i );
        size_t I_entry = getFactorEntry(I) - (_state[i] * I_skip);
        for( size_t st_i = 0; st_i < i_states; st_i++ ) {
            i_given_MB[st_i] *= f_I[I_entry];
            I_entry += I_skip;
        }
    }

    return i_given_MB.normalized();
}


inline void Gibbs::resampleVar( size_t i ) {
    // draw randomly from conditional distribution and update _state
    _state[i] = getVarDist(i).draw();
}


void Gibbs::randomizeState() {
    for( size_t i = 0; i < nrVars(); i++ )
        _state[i] = rnd_int( 0, var(i).states() - 1 );
}


void Gibbs::init() {
    for( size_t i = 0; i < nrVars(); i++ )
        fill( _var_counts[i].begin(), _var_counts[i].end(), 0 );
    for( size_t I = 0; I < nrFactors(); I++ )
        fill( _factor_counts[I].begin(), _factor_counts[I].end(), 0 );
    _sample_count = 0;
}


double Gibbs::run() {
    if( props.verbose >= 1 )
        cout << "Starting " << identify() << "...";
    if( props.verbose >= 3 )
        cout << endl;

    double tic = toc();
    
    randomizeState();

    for( size_t iter = 0; iter < props.iters; iter++ ) {
        for( size_t j = 0; j < nrVars(); j++ )
            resampleVar( j );
        updateCounts();
    }

    if( props.verbose >= 3 ) {
        for( size_t i = 0; i < nrVars(); i++ ) {
            cerr << "belief for variable " << var(i) << ": " << beliefV(i) << endl;
            cerr << "counts for variable " << var(i) << ": " << Prob( _var_counts[i].begin(), _var_counts[i].end() ) << endl;
        }
    }
    
    if( props.verbose >= 3 )
        cout << Name << "::run:  ran " << props.iters << " passes (" << toc() - tic << " clocks)." << endl;

    return 0.0;
}


inline Factor Gibbs::beliefV( size_t i ) const {
    return Factor( var(i), _var_counts[i].begin() ).normalized();
}


inline Factor Gibbs::beliefF( size_t I ) const {
    return Factor( factor(I).vars(), _factor_counts[I].begin() ).normalized();
}


Factor Gibbs::belief( const Var &n ) const {
    return( beliefV( findVar( n ) ) );
}


vector<Factor> Gibbs::beliefs() const {
    vector<Factor> result;
    for( size_t i = 0; i < nrVars(); ++i )
        result.push_back( beliefV(i) );
    for( size_t I = 0; I < nrFactors(); ++I )
        result.push_back( beliefF(I) );
    return result;
}


Factor Gibbs::belief( const VarSet &ns ) const {
    if( ns.size() == 1 )
        return belief( *(ns.begin()) );
    else {
        size_t I;
        for( I = 0; I < nrFactors(); I++ )
            if( factor(I).vars() >> ns )
                break;
        assert( I != nrFactors() );
        return beliefF(I).marginal(ns);
    }
}


} // end of namespace dai