#include "Parscript.h"

void main()
{
    std::string code{};
    std::string buff{};
    std::ifstream stream("tankscriptidea.pars");

    while (std::getline(stream, buff))
        code += buff + "\n";

    ParVm::Program program = ParVm::Compiler::Compile(code);

    for (int i = 0; i < program.programSize; ++i)
        std::cout << (int)program.pCode[i] << " ";

    std::cout << std::endl;

    struct v3
    {
        float x = 0.f, y = 0.f, z = 0.f;
    };

    struct
    {
        int DoneCounter    = 0U;
        float DeltaTime         = 0.1f;
        float ParticleLifeTime  =  25.f;
        int CoolInteger    = 2;
    }pGlobal{};

    struct
    {
        v3 pos { 1.f, 2.f, 3.f };
        v3 dir { 1.f, 1.f, 1.f };
        float gravity = -10.f;
        float lifetime = 0.f;
    }pWork[3]{};

    pWork->lifetime = 30.f; // This one will halt before the others

    for (int j = 0; j < 60 * 10; ++j)
        ParVm::Run(&program, &pGlobal, &pWork, 32, 3);

    std::cout << pWork[0].pos.x << " ";
    std::cout << pWork[1].pos.y << " ";
    std::cout << pWork[2].pos.z << "\n";

    std::cout << pWork[2].dir.x << " ";
    std::cout << pWork[1].dir.y << " ";
    std::cout << pWork[1].dir.z << "\n";

    std::cout << pWork[1].lifetime << std::endl;

    std::cout << pGlobal.DoneCounter << std::endl;
    std::cout << pGlobal.CoolInteger << std::endl;

    free(program.pCode);
}