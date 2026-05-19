////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_simulator_header.cpp
//	Created 	: 05.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator header
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_simulator_header.h"

CALifeSimulatorHeader::~CALifeSimulatorHeader() {}
void CALifeSimulatorHeader::save(IWriter& memory_stream)
{
    memory_stream.open_chunk(ALIFE_CHUNK_DATA);
    memory_stream.w_u32(ALIFE_VERSION);
    memory_stream.close_chunk();
}

void CALifeSimulatorHeader::load(IReader& file_stream)
{
    R_ASSERT2(file_stream.find_chunk(ALIFE_CHUNK_DATA), "Can't find chunk ALIFE_CHUNK_DATA");
    m_version = file_stream.r_u32();
    if (m_version < ALIFE_VERSION)
    {
        // Log before the assert so the message lands in the log even
        // if the user dismisses the assert dialog quickly. The bare
        // R_ASSERT2 message used to swallow the actual version numbers,
        // forcing a re-run with a debugger to see what mismatch happened.
        Msg("! ALife save format mismatch: save has version %u (0x%X), "
            "engine requires >= %u (0x%X). Delete the save and start a "
            "new game, or load with an older build.",
            m_version, m_version, ALIFE_VERSION, ALIFE_VERSION);
    }
    R_ASSERT2(m_version >= ALIFE_VERSION, "ALife version mismatch! (Delete saved game and try again)");
};

bool CALifeSimulatorHeader::valid(IReader& file_stream) const
{
    if (!file_stream.find_chunk(ALIFE_CHUNK_DATA))
        return (false);

    u32 version;
    file_stream.r(&version, sizeof(version));
    return (version >= ALIFE_VERSION);
}
