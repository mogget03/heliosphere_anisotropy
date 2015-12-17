#ifndef BASIC3D
#define BASIC3D

#include "Basic3DParams.h"
#include "DiffusionTensor.h"
#include "DriftVelocity.h"
#include "MagneticField.h"
#include "TrajectoryBase.h"
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/variate_generator.hpp>
#include <ctime>
#include <random>

/*!
 * Gets the sign of a number.
 * \param val value to get sign of.
 * \return sign of val, or 0 if val is 0.
 */
template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

// Makes the random number generator syntax cleaner
typedef boost::mt19937 MT19937;
typedef boost::normal_distribution<double> NDistribution;
typedef boost::variate_generator<MT19937, NDistribution> VariateGenerator;

/*!
 * Implements the basic 3D model found in doi:10.1088/0004-637X/735/2/83 (Strauss et al).
 */
class Basic3D : public TrajectoryBase<Basic3DParams> {
    public:
        //! Override base class constructor to set up random number generator.
        Basic3D(std::string paramFileName);

        /*!
         * Advances simulation by one timestep.
         */
        virtual Status step();

        virtual Status getStatus();

    protected:
        // Members are hidden during inheritance from a template class.  Since these variables are used a lot,
        // this seems better than using this->... or TrajectoryBase<Basic3DParams>::.
        using TrajectoryBase<Basic3DParams>::status;
        using TrajectoryBase<Basic3DParams>::params;
        using TrajectoryBase<Basic3DParams>::s;
        using TrajectoryBase<Basic3DParams>::r;
        using TrajectoryBase<Basic3DParams>::th;
        using TrajectoryBase<Basic3DParams>::ph;
        using TrajectoryBase<Basic3DParams>::ek;
        using TrajectoryBase<Basic3DParams>::P;
        using TrajectoryBase<Basic3DParams>::beta;
        using TrajectoryBase<Basic3DParams>::tanPsi;
        using TrajectoryBase<Basic3DParams>::sinPsi;
        using TrajectoryBase<Basic3DParams>::cosPsi;
        using TrajectoryBase<Basic3DParams>::gamma;

        //! Jupiter's angular position
        double phJup;

        //! Magnitude of magnetic field
        double bMag;
        //! Diffusion tensor.
        DiffusionTensor k;
        //! Drift velocity.
        DriftVelocity vd;

        //! Mersenne twister PRNG
        MT19937 engine;
        //! Normal distribution
        NDistribution distribution;
        //! Uses \a engine and \a distribution to generate Gaussian random numbers with mean 0 and standard
        //! deviation 1.
        VariateGenerator generator;

        /*!
         * Updates magnitude of magnetic field.
         */
        virtual void updateB();

        /*!
         * Updates diffusion tensor.
         */
        virtual void updateK();

        /*!
         * Updates drift velocity.
         */
        virtual void updateVdr();

        /*!
         * Updates convenience variables, magnetic field and diffusion tensor, in that order.
         */
        void updateVars();

    private:
        void updateKHelper();
};

Basic3D::Basic3D(std::string paramFileName) : TrajectoryBase<Basic3DParams>(paramFileName),
        phJup(params.ph0Jup()), engine(static_cast<unsigned int>(std::random_device{}())), distribution(0, 1),
        generator(engine, distribution) { }

void Basic3D::updateVars() {
    TrajectoryBase<Basic3DParams>::updateVars();
    // Only update diffusion tensor, etc after updating all other variables.  
    updateB();
    updateK();
    updateVdr();
}

Status Basic3D::getStatus() {
    TrajectoryBase<Basic3DParams>::getStatus();

    // Check whether particle ended in Jupiter's magnetosphere
    if (r > params.rBeginJup() && r < params.rEndJup()
            && ph > phJup - params.dphJup() && ph < phJup + params.dphJup()
            && th > params.thJup() - params.dthJup() && th < params.thJup() + params.dthJup()) {
        status = Status::Jupiter;
    }

    return status;
}

Status Basic3D::step() {
    // Don't both stepping if simulation has finished
    if (this->status == Status::Running) {
        // Updates everything
        updateVars();

        // Compute all the differentials
        double dr_ds = 2 / r * k.rr + k.rr_dr - (params.Vsw() + vd.r);
        double dr_dWr = sqrt(2 * k.rr - 2 * k.rph*k.rph / k.phph);
        double dr_dWph = k.rph * sqrt(2 / k.phph);

        double dth_ds = 1 / (r*r * tan(th)) * k.thth - vd.th / r;
        double dth_dWth = sqrt(2 * k.thth) / r;

        double dph_ds = 1 / (r*r * sin(th)) * k.rph + 1 / (r * sin(th)) * k.rph_dr - vd.ph / (r * sin(th));
        double dph_dWph = sqrt(2 * k.phph) / (r * sin(th));

        double dek_ds = 2 * params.Vsw() / (3 * r) * beta*beta * (ek + params.m());

        // Generate the Wiener terms
        double dWr = generator() * sqrt(params.ds());
        double dWth = generator() * sqrt(params.ds());
        double dWph = generator() * sqrt(params.ds());

#if DEBUG
        std::cout << "dr_ds = " << dr_ds << std::endl;
        std::cout << "dr_dWr = " << dr_dWr << std::endl;
        std::cout << "dr_dWph = " << dr_dWph << std::endl;
        std::cout << "dth_ds = " << dth_ds << std::endl;
        std::cout << "dth_dWth = " << dth_dWth << std::endl;
        std::cout << "dph_ds = " << dph_ds << std::endl;
        std::cout << "dph_dWph = " << dph_dWph << std::endl;
        std::cout << "dE_ds = " << dek_ds << std::endl;
#endif

        // Move the particle
        r += dr_ds * params.ds() + dr_dWr * dWr + dr_dWph * dWph;
        th += dth_ds * params.ds() + dth_dWth * dWth;
        ph += dph_ds * params.ds() + dph_dWph * dWph;
        ek += dek_ds * params.ds();
        // Move Jupiter
        phJup -= params.omegaJup() * params.ds();
        // Advance the backwards clock!
        s += params.ds();

        // Renormalize the angles
        TrajectoryBase<Basic3DParams>::renormalize();

        // Check the status
        getStatus();
    }

    return status;
}

void Basic3D::updateB() {
    // Magnitude of magnetic field
    bMag = params.b0() / (sqrt(1 + pow(1 - params.rSun(), 2)) * r*r * cosPsi);
}

void Basic3D::updateK() {
    // Temporarily increment r to numerically differentiate k
    r += params.deltar();
    // Update convenience variables that depend on r
    updatePsi();
    updateKHelper();
    double krr_r = k.rr;
    double krph_r = k.rph;

    // Compute tensor elements at actual point
    r -= params.deltar();
    updatePsi();
    updateKHelper();

    // Compute r derivatives
    k.rr_dr = (krr_r - k.rr) / params.deltar();
    k.rph_dr = (krph_r - k.rph) / params.deltar();

#if DEBUG
    std::cout << "Krr = " << k.rr
        << "\n\tKrr2 = " << krr_r
        << "\nKphph = " << k.phph
        << "\nKrph = " << k.rph
        << "\n\tKrph2 = " << krph_r
        << "\nKthth = " << k.thth
        << "\ndKrr_dr = " << k.rr_dr
        << "\n\tdelta_r = " << params.deltar()
        << "\ndKrph_dr = " << k.rph_dr << std::endl;
#endif
}

void Basic3D::updateKHelper() {
    // Parallel and perpendicular diffusion components
    double kpar = (speedOfLight*beta / 3) * params.lambda0() * (1 + r / params.rRefLambda())
        * (P > params.rig0() ? P / params.rig0() : 1);
    double kperp = params.kperp_kpar() * kpar;
#if DEBUG
    std::cout << "kpar = " << kpar << std::endl;
    std::cout << "kperp = " << kperp << std::endl;
    std::cout << "cosPsi = " << cosPsi << std::endl;
    std::cout << "sinPsi = " << sinPsi << std::endl;
#endif

    k.rr = kpar * cosPsi*cosPsi + kperp * sinPsi*sinPsi; // TODO: make an abstact base class for 3D models...
    k.phph = kpar * sinPsi*sinPsi + kperp * cosPsi*cosPsi; // kperp = kperp,r
    k.rph = (kperp - kpar) * cosPsi * sinPsi; // kperp = kperp,r
    k.thth = kperp; // kperp = kperp,th
}

void Basic3D::updateVdr() {
    // Sign of drift velocity.  Changes as the particle passes through the HCS.
    double vdSign = 1;

    // Heaviside function comes in here
    if (th > M_PI / 2) {
        vdSign = -1;
    } else if (th == M_PI / 2) {
        vdSign = 0;
    }

    // Gradient and curvature part of drift velocity
    double vdCoeff = 4.468e-5 * (2 * P * beta * r) // (1 GV) / (1 nT * 1 au) = 4.468e-5 au / s
        / (3 * pow(1 + gamma*gamma, 2) * sgn(params.charge()) * params.Ac() * params.b0()
                * pow(params.r0(), 2));
    vd.r = vdSign * vdCoeff * (-gamma / tan(th));
    vd.th = vdSign * vdCoeff * (2 + gamma*gamma) * gamma;
    vd.ph = vdSign * vdCoeff * gamma*gamma / tan(th);

    // Compute HCS part of drifts
    double d = fabs(r * cos(th));
    double rL = P / bMag * 0.0223; // (1 GV/c) / (1 nT) = 0.0223 au

    if (d <= 2 * rL) {
        double hcsDriftFact = params.Ac() * sgn(params.charge())
            * (0.457 - 0.412 * d / rL + 0.0915 * pow(d / rL, 2)) * speedOfLight*beta;
        vd.r += hcsDriftFact * sinPsi;
        vd.ph += hcsDriftFact * cosPsi;
#if DEBUG
        std::cout << "hcsDriftFact = " << hcsDriftFact << std::endl;
#endif
    }

    // Drift reduction factor
    double fs = 10 * pow(P / params.rig0(), 2) / (1 + 10 * pow(P / params.rig0(), 2));

    vd.r *= fs;
    vd.th *= fs;
    vd.ph *= fs;

#if DEBUG
    std::cout << "vdCoeff = " << vdCoeff
        << "\nvdr = " << vd.r
        << "\nvdth = " << vd.th
        << "\nvdph = " << vd.ph << std::endl;
#endif
}

#endif


