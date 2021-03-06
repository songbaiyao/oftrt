/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2016 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    icoFoam

Description
    Transient solver for incompressible, laminar flow of Newtonian fluids.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "pisoControl.H"

#include "fpe.H"
// #include "sampleUffMNIST.H"
#include "uffModel.H"
samplesCommon::Args args;
MNISTSampleParams params = initializeSampleParams(args);

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
#include "setRootCase.H"
#include "createTime.H"
#include "createMesh.H"

    pisoControl piso(mesh);

#include "createFields.H"
#include "initContinuityErrs.H"

    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

    Info << "\nStarting time loop\n"
         << endl;

    // initial model object
    params.dataDirs.push_back("./data/");
    params.batchSize = 1024;

    uffModel sample(params);

    FPExceptionsGuard fpguard;
    sample.build();

    while (runTime.loop())
    {
        Info << "Time = " << runTime.timeName() << nl << endl;

        std::vector<float> output_real;
        float m_in[2] = {4.20000000e+01, -1.24467032e+05};
        float s_in[2] = {4.89897949e+00, 1.23205254e+05};

        const int scaling = 1;
        static std::vector<float> input_p_he;
        input_p_he.clear();
        int i = 0;
        const cellList &cells = mesh.cells();
        forAll(cells, celli)
        {
            for (int j = 0; j < scaling; j++)
            {
                // input_p_he[i++] = (in_1[celli] / 1e5 - m_in[0]) / s_in[0];
                // input_p_he[i++] = (in_2[celli] - m_in[1]) / s_in[1];

                input_p_he.emplace_back((in_1[celli] / 1e5 - m_in[0]) / s_in[0]);
                input_p_he.emplace_back((in_2[celli] - m_in[1]) / s_in[1]);
            }
        }
        std::cout << "input size: " << input_p_he.size() << '\n';
        auto t_start = std::chrono::high_resolution_clock::now();
        bool success = sample.infer(input_p_he, output_real);
        auto t_end = std::chrono::high_resolution_clock::now();
        auto total = std::chrono::duration<float, std::milli>(t_end - t_start).count();
        std::cout << "inference takes " << total << "ms.\n";
        std::cout << success
                  << " uff inference finished.\n";
        std::cout << "output size " << output_real.size() << "\n";
        float m_out[4] = {1.69567010e+02, 2.19935016e+02, 2.59815793e-05, 1.72609032e+03};
        float s_out[4] = {2.26427039e+02, 7.50554975e+01, 2.89748538e-05, 1.51159205e+03};
        forAll(cells, celli)
        {
            out_1[celli] = output_real[celli * 4] * s_out[0] + m_out[0];
            out_2[celli] = output_real[celli * 4 + 1] * s_out[1] + m_out[1];
            out_3[celli] = output_real[celli * 4 + 2] * s_out[2] + m_out[2];
            out_4[celli] = output_real[celli * 4 + 3] * s_out[3] + m_out[3];
        }

#include "CourantNo.H"

        // Momentum predictor

        fvVectorMatrix UEqn(
            fvm::ddt(U) + fvm::div(phi, U) - fvm::laplacian(nu, U));

        if (piso.momentumPredictor())
        {
            solve(UEqn == -fvc::grad(p));
        }

        // --- PISO loop
        while (piso.correct())
        {
            volScalarField rAU(1.0 / UEqn.A());
            volVectorField HbyA(constrainHbyA(rAU * UEqn.H(), U, p));
            surfaceScalarField phiHbyA(
                "phiHbyA",
                fvc::flux(HbyA) + fvc::interpolate(rAU) * fvc::ddtCorr(U, phi));

            adjustPhi(phiHbyA, U, p);

            // Update the pressure BCs to ensure flux consistency
            constrainPressure(p, U, phiHbyA, rAU);

            // Non-orthogonal pressure corrector loop
            while (piso.correctNonOrthogonal())
            {
                // Pressure corrector

                fvScalarMatrix pEqn(
                    fvm::laplacian(rAU, p) == fvc::div(phiHbyA));

                pEqn.setReference(pRefCell, pRefValue);

                pEqn.solve(mesh.solver(p.select(piso.finalInnerIter())));

                if (piso.finalNonOrthogonalIter())
                {
                    phi = phiHbyA - pEqn.flux();
                }
            }

#include "continuityErrs.H"

            U = HbyA - rAU * fvc::grad(p);
            U.correctBoundaryConditions();
        }

        runTime.write();

        Info << "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
             << "  ClockTime = " << runTime.elapsedClockTime() << " s"
             << nl << endl;
    }
    sample.teardown();
    Info << "End\n"
         << endl;

    return 0;
}

// ************************************************************************* //
