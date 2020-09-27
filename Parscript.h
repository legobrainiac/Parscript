#ifndef PAR_SCRIPT_H
#define PAR_SCRIPT_H

#include <map>
#include <tuple>
#include <array>
#include <string>

// TODO(tomas): linking to user-given functions, might require some rethinking

namespace ParVm
{
    struct Scope
    {
        [[maybe_unused]] uint64_t scopeSize;
        std::map<std::string, uint64_t> m_ScopeOffsetResolver{};
    };

    class Program
    {
    public:
        uint64_t programCounter = 0U;
        uint64_t programSize    = 0U;
        uint8_t *pCode          = nullptr;
    };

    class Compiler
    {
    private:
        [[nodiscard]] static std::string GetOperandString(const std::string &function, uint64_t operandIdx = 0) noexcept
        {
            // NOTE(tomas): consider switching over to regex for extraction
            // Trim
            int64_t functionStart = function.find('(');
            std::string trimmed = function.substr(functionStart + 1);
            trimmed = trimmed.substr(0, trimmed.size() - 1);

            // Extract
            int64_t operandBeg = 0;
            for(int comIdx = 0; comIdx < operandIdx; ++comIdx)
                operandBeg = trimmed.find(',', operandBeg + 1);

            int64_t endComma = trimmed.find(',', operandBeg + 1);

            if(endComma != -1)
                trimmed = trimmed.substr(operandBeg, endComma - operandBeg);
            else
                trimmed = trimmed.substr(operandBeg);

            if (operandBeg != 0)
                trimmed = trimmed.substr(1);

            return trimmed;
        }

        [[nodiscard]] static std::string Sanitize(const std::string& code)
        {
            ////////////////////////////////////////////////////////////////
            // Sanitize code
            std::string sanitizedCode = code;

            // Remove comments
            uint64_t currentIndex = 0U;
            while (true)
            {
                currentIndex = sanitizedCode.find("//", currentIndex + 1U);

                // If no more comment, end
                if (currentIndex == -1)
                    break;

                // Otherwise, find line break and set everything to spaces untill there
                uint64_t lineBreak = sanitizedCode.find('\n', currentIndex);

                if (lineBreak == -1)
                    throw std::exception("Invalid line endings...");

                for (int i = currentIndex; i < lineBreak; ++i)
                    sanitizedCode[i] = ' ';
            }

            // Remove whitespace
            sanitizedCode.erase(std::remove_if(sanitizedCode.begin(), sanitizedCode.end(), std::isspace), sanitizedCode.end());

            return sanitizedCode;
        }

        [[nodiscard]] static std::array<Scope, 3> FindScopes(const std::string& sanitizedCode)
        {
            ////////////////////////////////////////////////////////////////
            // Find scopes
            const auto FindScope = [&sanitizedCode](const std::string &scopeIdentifier) -> Scope {
                Scope scope{};

                ////////////////////////////////////////////////////////////////
                // Get Scope start and end
                int64_t scopeStart = sanitizedCode.find(scopeIdentifier);

                if (scopeStart == -1)
                    throw std::exception(("Missing scope -> " + scopeIdentifier).c_str());

                ////////////////////////////////////////////////////////////////
                // Get scope size
                int64_t scopeSizeStart = sanitizedCode.find('[', scopeStart);
                int64_t scopeSizeEnd = sanitizedCode.find("]]", scopeStart);

                if (scopeSizeStart == -1 || scopeSizeEnd == -1)
                    throw std::exception(
                            ("Malformed scope, missing size specifier for scope -> " + scopeIdentifier).c_str());

                int64_t scopeSize = std::stoi(
                        sanitizedCode.substr(scopeSizeStart + 1U, scopeSizeEnd - scopeSizeStart));

                ////////////////////////////////////////////////////////////////
                // Get scope identifiers
                int64_t identifierStart = scopeSizeEnd + 2;
                int64_t identifierEnd = sanitizedCode.find("};", identifierStart);

                if (sanitizedCode[identifierStart] != '{')
                    throw std::exception(("Malformed scope field, missing { for scope -> " + scopeIdentifier).c_str());

                auto scopeFields = sanitizedCode.substr(identifierStart + 1u, (identifierEnd - identifierStart) - 1U);

                // Extract name and offset from a given field string, ie: "[0]->GlobalCounter;"
                const auto ExtractField = [](
                        const std::string &fieldString) -> std::vector<std::pair<std::string, uint64_t>>
                {
                    std::pair<std::string, uint64_t> current{};
                    std::vector<std::pair<std::string, uint64_t>> out{};

                    // Get name
                    int64_t identifierArrow = fieldString.find('>');

                    // Is this an array identifier?
                    int64_t arrayIdentifier = fieldString.find('[', identifierArrow);

                    if (arrayIdentifier != -1)
                    {
                        // This is a complex field, we need to extract an array of identifiers
                        auto offsets = fieldString.substr(1U, fieldString.find(']') - 1U);
                        auto subFieldNames = fieldString.substr(arrayIdentifier + 1,
                                                                (fieldString.find("];") - arrayIdentifier) - 1);
                        auto primaryFieldName = fieldString.substr(identifierArrow + 1,
                                                                   (fieldString.find('[', identifierArrow) -
                                                                    identifierArrow) - 1);

                        std::string previous{};

                        // Process this complex field and populate the out vector
                        while (previous != offsets)
                        {
                            int64_t offsetComma = offsets.find(',');
                            int64_t subfieldComma = subFieldNames.find(',');

                            current.first = primaryFieldName + "." + subFieldNames.substr(0U, subfieldComma);
                            current.second = std::stoi(offsets.substr(0U, offsetComma));

                            // Shitty way to stop the loop xd
                            previous = offsets;

                            // Set up next iteration
                            offsets = offsets.substr(offsetComma + 1);
                            subFieldNames = subFieldNames.substr(subfieldComma + 1);

                            out.push_back(current);
                            current = {};
                        }
                    }
                    else
                    {
                        // This is a simple field, we just get the information and head out
                        current.first = fieldString.substr(identifierArrow + 1U,
                                                           (fieldString.size() - identifierArrow) - 2);

                        // Get offset
                        int64_t sizeClosingBracket = fieldString.find(']');
                        current.second = std::stoi(fieldString.substr(1U, sizeClosingBracket - 1U));

                        out.push_back(current);
                    }

                    return out;
                };

                // Perform extraction
                while (!scopeFields.empty())
                {
                    uint64_t semiColon = scopeFields.find(';');
                    auto fieldString = scopeFields.substr(0U, semiColon + 1);
                    auto fields = ExtractField(fieldString);

                    for (auto &f : fields)
                        scope.m_ScopeOffsetResolver.insert(f);

                    scopeFields = scopeFields.substr(semiColon + 1);
                }

                scope.scopeSize = scopeSize;

                return scope;
            };

            return std::array<Scope, 3>{ FindScope("GlobalScope"), FindScope("WorkScope"), FindScope("LocalScope") };
        }

        [[nodiscard]] static std::vector<std::string> ExtractInstructions(const std::string& code)
        {
            std::string sanitizedCode = code;

            // After the scopes are found, we're ready to compile
            // First we find the start of the worker function
            int64_t workerStart = sanitizedCode.find("Worker");

            if(workerStart == -1)
                throw std::exception("Worker function missing...");

            // Clip sanitizedCode
            sanitizedCode = sanitizedCode.substr(workerStart);

            // Find code start and code end
            int64_t codeStart  = sanitizedCode.find('{');
            int64_t codeEnd    = sanitizedCode.find("};");

            if(codeStart == -1)
                throw std::exception("Opening { missing");

            if(codeEnd == -1)
                throw std::exception("Closing }; missing");

            sanitizedCode = sanitizedCode.substr(codeStart + 1U, codeEnd - codeStart);

            // Split code in to lines
            std::vector<std::string> lines{};

            while (true)
            {
                uint64_t instructionEnd = sanitizedCode.find(';');

                if(instructionEnd == -1)
                    break;

                lines.push_back(sanitizedCode.substr(0, instructionEnd));
                sanitizedCode = sanitizedCode.substr(instructionEnd + 1);
            }

            return lines;
        }

    public:
        #define OperandPush(OP_IDX)\
        auto [opl, opr] = SOResolver(GetOperandString(function, OP_IDX));\
        program.push_back(opl); \
        program.push_back(opr); \

        #define AssignmentPush()\
        int64_t assignmentEnd = function.find('=');\
        std::string name = function.substr(0, assignmentEnd);\
        auto [opl, opr] = SOResolver(name);\
        program.push_back(opl);\
        program.push_back(opr);\

        #define CompilerResolver(OP_CODE, ASSIGNS, OPERAND_COUNT)\
        [&SOResolver, &program](const std::string& function) {\
        program.push_back(OP_CODE);\
        if constexpr (ASSIGNS) { AssignmentPush() }\
        if constexpr (OPERAND_COUNT > 0) { for(int idx = 0; idx < OPERAND_COUNT; ++idx) { OperandPush(idx) } }\
        }\

        [[nodiscard]] static Program Compile(const std::string& code)
        {
            ////////////////////////////////////////////////////////////////
            // Cleanup code and extract the primary working data
            auto sanitizedCode = Sanitize(code);
            auto scopes = FindScopes(sanitizedCode);
            auto lines = ExtractInstructions(sanitizedCode);

            std::vector<uint8_t> program{};

            ////////////////////////////////////////////////////////////////
            // Scope and Offset resolver
            const auto SOResolver = [&scopes](const std::string& variableName) -> std::pair<uint64_t, uint64_t>
            {
                int64_t scope = 0;
                for(auto& s : scopes)
                {
                    auto it = s.m_ScopeOffsetResolver.find(variableName);
                    if(it != s.m_ScopeOffsetResolver.end())
                        return std::make_pair(scope, it->second);

                    ++scope;
                }

                throw std::exception((std::string("Failed to resolve variable scope and offset... ") + variableName).c_str());
            };

            ////////////////////////////////////////////////////////////////
            // Function resolvers
            typedef std::function<void(const std::string&)> Resolver;
            std::unordered_map<std::string, std::unordered_map<std::string, Resolver>> resolvers {};

            // Float arithmetic ///////////////////////////////////////////
            resolvers["Float"]["::++"]  = CompilerResolver(1, false, 1);    // INC_FLOAT [&Scope + Offset]
            resolvers["Float"]["::--"]  = CompilerResolver(2, false, 1);    // DEC_FLOAT [&Scope + Offset]
            resolvers["Float"]["::+"]   = CompilerResolver(3, true, 2);     // ADD_FLOAT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Float"]["::-"]   = CompilerResolver(4, true, 2);     // SUB_FLOAT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Float"]["::*"]   = CompilerResolver(5, true, 2);     // MUL_FLOAT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Float"]["::>"]   = CompilerResolver(14, true, 2);    // BIGGER_THAN_FLOAT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Float"]["::<"]   = CompilerResolver(15, true, 2);    // SMALLER_THAN_FLOAT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]

            // Integer arithmetic /////////////////////////////////////////
            resolvers["Int"]["::++"]   = CompilerResolver(6, false, 1);     // INC_INT [&Scope + Offset]
            resolvers["Int"]["::--"]   = CompilerResolver(7, false, 1);     // DEC_INT [&Scope + Offset]
            resolvers["Int"]["::+"]    = CompilerResolver(10, true, 2);     // ADD_INT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Int"]["::-"]    = CompilerResolver(11, true, 2);     // SUB_INT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]
            resolvers["Int"]["::*"]    = CompilerResolver(12, true, 2);     // MUL_INT [&Scope + Offset], [&Scope + Offset], [&Scope + Offset]

            // Vm Instructions ////////////////////////////////////////////
            resolvers["VM"]["::Halt"]  = CompilerResolver(0, false, 0);             // PAR_HALT
            resolvers["VM"]["::HaltConditional"] = CompilerResolver(13, false, 1);  // PAR_HALT_CONDITIONAL [&Scope + Offset]

            // TODO(tomas): add some debug functionality: Breakpoint, Log ProgramCounter, Reset local scope

            ////////////////////////////////////////////////////////////////
            // Decode and assemble instructions in to bytecode
            for(const auto& line : lines)
            {
                // Get start of function scope
                int64_t scopeStart = line.find('=');
                scopeStart = (scopeStart == -1) ? 0 : scopeStart + 1;

                // Get end of function scope
                int64_t scopeEnd = line.find("::");
                if(scopeEnd == -1)
                    throw std::exception("Failed to resolve function scope...");

                // Get end of function name
                int64_t fnEnd = line.find('(');

                std::string scope = line.substr(scopeStart, scopeEnd - scopeStart);
                std::string fName = line.substr(scopeEnd, fnEnd - scopeEnd);

                // Find the correct resolver
                if(resolvers.contains(scope))
                {
                    auto[key, funResolvers] = *resolvers.find(scope);
                    auto it = funResolvers.find(fName);

                    if(it != funResolvers.end())
                        it->second(line);
                }
            }

            ////////////////////////////////////////////////////////////////
            // Add halt to the end
            program.push_back(0);

            ////////////////////////////////////////////////////////////////
            // Write out program
            Program p{};
            p.programCounter = 0;
            p.programSize = program.size();
            p.pCode = static_cast<uint8_t*>(malloc(program.size()));
            std::memcpy(p.pCode, program.data(), program.size());
            return p;
        }
    };

    // Runs inline
    void Run(Program *pProgram, void *pGlobalScope, void *pWorkScopes, uint64_t workScopeSize, uint64_t workScopeCount = 1, bool zeroLocalScope = true)
    {
        ////////////////////////////////////////////////////////////////
        // Boilerplate helpers
        #define LOCAL_SCOPE_SIZE 256

        #define WORK_SCOPE      1
        #define LOCAL_SCOPE     2

        #define Step(StepCount)                 pProgram->programCounter += StepCount; goto *opLut[*(pProgram->pCode + (pProgram->programCounter))]
        #define Operand(OpOffset)               pProgram->pCode[pProgram->programCounter + (OpOffset + 1)]
        #define DeclareOp(OpName, Size, Code)   OpName:{Code}Step(Size);

        ////////////////////////////////////////////////////////////////
        // Setup part Virtual machine
        uint64_t workUnitIdx = 0U;

        if(workScopeCount <= 0U)
            return;

        std::array<uint8_t*, 3> pScopes
        {
            static_cast<uint8_t*>(pGlobalScope),
            static_cast<uint8_t*>(pWorkScopes),
            new uint8_t [LOCAL_SCOPE_SIZE]
        };

        if(zeroLocalScope)
            std::memset(pScopes[LOCAL_SCOPE], 0, LOCAL_SCOPE_SIZE);

        static constexpr void* opLut[] = {
            &&PAR_HALT,
            &&INC_FLOAT,
            &&DEC_FLOAT,
            &&ADD_FLOAT,
            &&SUB_FLOAT,
            &&MUL_FLOAT,
            &&INC_INT,
            &&DEC_INT,
            &&INC_UINT,
            &&DEC_UINT,
            &&ADD_INT,
            &&SUB_INT,
            &&MUL_INT,
            &&PAR_HALT_CONDITIONAL,
            &&BIGGER_THAN_FLOAT,
            &&SMALLER_THAN_FLOAT
        };

        ////////////////////////////////////////////////////////////////
        // Start pars Virtual machine
        goto *opLut[pProgram->pCode[pProgram->programCounter]];

        ////////////////////////////////////////////////////////////////
        // VM Instructions
        DeclareOp(PAR_HALT, 1, // PAR_HALT
                ++workUnitIdx;
                if(workUnitIdx >= workScopeCount) {
                    // Work is done, cleanup memory and exit VM
                    delete pScopes[LOCAL_SCOPE];
                    return;
                }
                else {
                    // Reset the PC to 0 and advance work unit
                    pProgram->programCounter = 0U;
                    pScopes[WORK_SCOPE] = static_cast<uint8_t*>(pWorkScopes) + (workUnitIdx * workScopeSize);

                    // Zero local scope if needed
                    if(zeroLocalScope)
                        std::memset(pScopes[LOCAL_SCOPE], 0, LOCAL_SCOPE_SIZE);

                    // Start VM on the new local scope work unit, this skips the Step op
                    goto *opLut[pProgram->pCode[pProgram->programCounter]];
                });

        DeclareOp(PAR_HALT_CONDITIONAL,     3, { auto* l = (bool*)(pScopes[Operand(0U)] + Operand(1U)); if(*l) goto *opLut[0]; });

        ////////////////////////////////////////////////////////////////
        // Floating point arithmetic instructions
        DeclareOp(INC_FLOAT,            3,{ auto* l = (float*)(pScopes[Operand(0U)] + Operand(1U)); *l += 1.f; }); // INC_FLOAT_CONTENT &Scope + offset
        DeclareOp(DEC_FLOAT,            3,{ auto* l = (float*)(pScopes[Operand(0U)] + Operand(1U)); *l -= 1.f; }); // DEC_FLOAT_CONTENT &Scope + offset
        DeclareOp(ADD_FLOAT,            7,{ auto* l = (float*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((float*)((pScopes[Operand(2U)] + Operand(3U)))) + *((float*)((pScopes[Operand(4U)] + Operand(5U)))); }); // ADD_FLOAT &Scope + offset, &Scope + offset
        DeclareOp(SUB_FLOAT,            7,{ auto* l = (float*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((float*)((pScopes[Operand(2U)] + Operand(3U)))) - *((float*)((pScopes[Operand(4U)] + Operand(5U)))); }); // SUB_FLOAT &Scope + offset, &Scope + offset
        DeclareOp(MUL_FLOAT,            7,{ auto* l = (float*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((float*)((pScopes[Operand(2U)] + Operand(3U)))) * *((float*)((pScopes[Operand(4U)] + Operand(5U)))); }); // MUL_FLOAT &Scope + offset, &Scope + offset, &Scope + offset
        DeclareOp(BIGGER_THAN_FLOAT,    7,{ auto* l = (bool*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((float*)((pScopes[Operand(2U)] + Operand(3U)))) > *((float*)((pScopes[Operand(4U)] + Operand(5U)))); });
        DeclareOp(SMALLER_THAN_FLOAT,   7,{ auto* l = (bool*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((float*)((pScopes[Operand(2U)] + Operand(3U)))) < *((float*)((pScopes[Operand(4U)] + Operand(5U)))); });

        ////////////////////////////////////////////////////////////////
        // Integer arithmetic instructions
        DeclareOp(INC_INT,              3,{ auto* l = (int*)(pScopes[Operand(0U)] + Operand(1U)); *l += 1; }); // INC_INT_CONTENT &Scope + offset
        DeclareOp(DEC_INT,              3,{ auto* l = (int*)(pScopes[Operand(0U)] + Operand(1U)); *l -= 1; }); // DEC_INT_CONTENT &Scope + offset
        DeclareOp(INC_UINT,             3,{ auto* l = (unsigned int*)(pScopes[Operand(0U)] + Operand(1U)); *l += 1U; }); // INC_UINT_CONTENT &Scope + offset
        DeclareOp(DEC_UINT,             3,{ auto* l = (unsigned int*)(pScopes[Operand(0U)] + Operand(1U)); *l -= 1U; }); // DEC_UINT_CONTENT &Scope + offset
        DeclareOp(ADD_INT,              7,{ auto* l = (int*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((int*)((pScopes[Operand(2U)] + Operand(3U)))) + *((int*)((pScopes[Operand(4U)] + Operand(5U)))); }); // ADD_INT &Scope + offset, &Scope + offset
        DeclareOp(SUB_INT,              7,{ auto* l = (int*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((int*)((pScopes[Operand(2U)] + Operand(3U)))) - *((int*)((pScopes[Operand(4U)] + Operand(5U)))); }); // SUB_INT &Scope + offset, &Scope + offset
        DeclareOp(MUL_INT,              7,{ auto* l = (int*)(pScopes[Operand(0U)] + Operand(1U)); *l = *((int*)((pScopes[Operand(2U)] + Operand(3U)))) * *((int*)((pScopes[Operand(4U)] + Operand(5U)))); }); // MUL_FLOAT &Scope + offset, &Scope + offset, &Scope + offset
    }
};

#endif // !PAR_SCRIPT_H
