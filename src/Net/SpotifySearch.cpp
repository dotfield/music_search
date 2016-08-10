/*
 * SpotifySearch.cpp
 *
 *  Created on: 14 Feb 2016
 *      Author: neil
 */

#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
//#include <Poco/JSON/JSON.h>
#include <Poco/JSON/Parser.h>

#include <Poco/Path.h>
#include <Poco/URI.h>
#include <Poco/Exception.h>

#include <iostream>
#include <string>
#include <cctype>
#include <Utility/strutils.h>
#include <Utility/fileutils.h>
#include <Utility/Message.h>
#include <Utility/Output.h>
#include <Utility/input.h>
#include <IOC/Runnable.h>
#include <IOC/BuilderNParams.h>
#include <unordered_set>


	using Poco::Dynamic::Var;
	using namespace Poco::JSON;
	const Poco::Net::Context::Ptr context( new Poco::Net::Context( Poco::Net::Context::CLIENT_USE, "", "", "",Poco::Net::Context::VERIFY_NONE ) );


class SpotifySearch : public IOC::Runnable
{

	Poco::URI m_uri;
	Utility::OutputPtr m_output;
	size_t m_maxTrack;
	std::unordered_set< std::string > m_excludedIds;
	size_t m_startOffset;

	Poco::Net::HTTPSClientSession m_session;

public:
	SpotifySearch( Utility::OutputPtr output, size_t maxTrack,
			std::vector< std::string > const& exclusionFiles,
			size_t startOffset )
		: m_uri( "https://api.spotify.com" )
		, m_output( output )
		, m_maxTrack( maxTrack )
		, m_startOffset( startOffset )
		, m_session( m_uri.getHost(), m_uri.getPort(), context )
	{
		for( auto const& filename : exclusionFiles )
		{
			std::ifstream ifs( filename );
			if( !ifs.is_open() )
			{
				Utility::Message() << "Unable to open input file " << filename << Utility::ThrowMessage<std::runtime_error>;
			}
			std::string line;
			while( std::getline( ifs, line ) )
			{
				Utility::StrTuple term = Utility::delimitedText( line, '\t' );
				std::string token = term[0];
				if( token[0] != 's' )
					token = term[1];
				m_excludedIds.insert( token ); // we don't care if there are duplicates
			}
		}
	}

	bool searchRun( int& offset )
	{
		Poco::Net::HTTPResponse response;
		std::string rsStr;
		for( int limit=50 ; limit && rsStr.empty(); --limit )
		{
			std::ostringstream oss;
			oss << "https://api.spotify.com/v1/search?query=tag%3Anew&offset=" << offset << ""
					"&limit=" << limit << "&type=album&market=GB";

			std::string requestStr( oss.str());
			Poco::Net::HTTPRequest request( Poco::Net::HTTPRequest::HTTP_GET,
					requestStr, Poco::Net::HTTPMessage::HTTP_1_1 );

			m_session.sendRequest( request );
			std::istream& rs = m_session.receiveResponse( response );
			std::string rsStr0(std::istreambuf_iterator<char>(rs), {});

			if( rsStr0.empty() )
			{
				std::cout << "No results, request=" << requestStr << std::endl;
				m_session.reset();
				sleep( 10 );
			}
			rsStr.swap( rsStr0 );
		}
		if( rsStr.empty() )
		{
			std::cout << "Try again later" << std::endl;
			return false;
		}
		Parser parser;
		Var result;

		try
		{
			result = parser.parse(rsStr);
		}
		catch( const std::exception& err )
		{
			std::cerr << "Error " << err.what() << " Response: ";

			response.write( std::cerr );

			std::cerr << rsStr << std::endl;

			return false;
		}

		Object::Ptr obj = result.extract<Object::Ptr>();

		if( !obj )
		{
			std::cerr << "Could not extract object" << std::endl;
		}
		Object::Ptr albums = obj->getObject( "albums" );
		Array::Ptr arr = albums->getArray( "items" );

		if( !arr )
		{
			std::cerr << "Could not get items" << std::endl;
			return false;
		}
		else
		{
			size_t count = arr->size();
			if( count == 0 )
			{
				return false;
			}
			size_t output = 0;
			for( size_t i = 0; i < count; ++i )
			{
				Object::Ptr result = arr->getObject( i );
				if( !result )
				{
					std::cerr << "Could not get result " << i << std::endl;
					continue;
				}

				Var item = result->get( "id");
				if( item.isString() )
				{
					std::string albumId = item.convert< std::string >();
					if( m_excludedIds.count( albumId ) )
					{
						continue;
					}
					else
					{
						output += addAlbum( albumId );
					}
				}
			}
			std::cout << offset << ": " << output << " out of "
					<< count << " added" << std::endl;

			offset += count;
		}
		return true;
	}

	size_t addAlbum( const std::string& albumId )
	{
		try
		{
			std::ostringstream oss;
			oss << "https://api.spotify.com" <<
					"/v1/albums/" << albumId << "/tracks?offset=0&limit=1";


			Poco::Net::HTTPRequest request( Poco::Net::HTTPRequest::HTTP_GET,
					oss.str(), Poco::Net::HTTPMessage::HTTP_1_1 );

			Poco::Net::HTTPResponse response;
			m_session.sendRequest( request );
			std::istream& rs = m_session.receiveResponse( response );
			std::string rsStr(std::istreambuf_iterator<char>(rs), {});

			Parser parser;
			Var result = parser.parse(rsStr);
			Object::Ptr obj = result.extract<Object::Ptr>();
			Var item = obj->get( "total" );
			if( item.isInteger() )
			{
				int total = item.convert< int >();
				if( total > m_maxTrack )
				{
					return 0; // we don't want this one
				}
				else
				{
					Array::Ptr arr = obj->getArray( "items" );
					Object::Ptr firstTrack = arr->getObject( 0 );
					Array::Ptr artists = firstTrack->getArray( "artists" );
					size_t numArtists = artists->size();
					Var duration = firstTrack->get( "duration_ms" );
					int length = duration.convert< int >();

					if( length > 360000 )
					{
						return 0;
					}

					std::ostream& os( m_output->os() );

					// album-id, track uri, artist(s), title
					// number of tracks, length of track in secs
					os << albumId << '\t' <<
							firstTrack->get( "uri" ).convert< std::string >()
							<< '\t';

					for( size_t i = 0; i < numArtists; ++i )
					{
						Object::Ptr artist = artists->getObject( i );
						if( i > 0 )
						{
							os << " & ";
						}
						os << artist->get( "name" ).convert< std::string >();
					}

					os << '\t' << firstTrack->get("name").convert<std::string>()
							<< '\t' << total << '\t' <<
							length / 1000 << '\n';

					m_output->flush();

					return 1;
				}
			}
			else
			{
				std::cerr << "total number of tracks not an integer" << std::endl;
				return 0;
			}
		}
		catch( std::exception const& err )
		{
			std::cerr << "Error " << err.what() <<
					" parsing album id " << albumId
					<< std::endl;

			return 0;
		}
	}

protected:
	int doRun()
	{
		int offset = m_startOffset;
		while( searchRun( offset ) );

		return 0;
	}

};

using namespace IOC;
typedef Builder4Params< SpotifySearch, Runnable, Utility::Output,
		size_t, std::vector< std::string >, size_t > SpotifySearchBuilder;

extern "C" {

IOC_API IOC::BuilderFactoryImpl< SpotifySearchBuilder > g_SpotifySearch;

}



