/***********************************************************************

Marlin: A Fast Entropy Codec

MIT License

Copyright (c) 2017 Manuel Martinez Torres

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

***********************************************************************/


#ifndef MARLIN_H
#define MARLIN_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

struct MarlinDictionary;

#if defined (__cplusplus)
extern "C" {
#endif

/*! 
 * Compresses src to dst using dictionary dict.
 * 
 * \param dst output buffer
 * \param dstCapacity allocated capacity of dst
 * \param src input buffer
 * \param srcSize input buffer size
 * \param dict dictionary to use for compression
 * 
 * \return negative: error occurred
 *         0: if data is not compressible
 *         1: if data is a repetition of a single byte
 *         positive: size of the compressed buffer
*/
ssize_t Marlin_compress(const MarlinDictionary *dict, uint8_t* dst, size_t dstCapacity, const uint8_t* src, size_t srcSize);

/*! 
 * Uncompresses src to dst using dictionary dict.
 * 
 * \param dst output buffer
 * \param dstSize ouput buffer size
 * \param src input buffer
 * \param srcSize input buffer size
 * \param dict dictionary to use for decompression
 * 
 * \return negative: error occurred
 *         positive: number of uncompressed bytes (must match dstSize
*/
ssize_t Marlin_decompress(const MarlinDictionary *dict, uint8_t* dst, size_t dstSize, const uint8_t* src, size_t srcSize);

/*! 
 * Builds an optimal for a 8 bit memoryless source. Dictionary must be freed with Marlin_free_dictionary.
 * 
 * \param hist histogram of symbols in the 8 bit alphabet
 * \param name an identificator for the dictionary (max size 15 bytes).
 * \param indexSizeBits number of bits on the index. Must be larger than 8-rawStorageBits.
 * \param indexOverlapBits number of bits of overlap. Suggested small.
 * \param maxWordSizeSymbols maximum amount of non zero symbols per word.
 * \param rawStorageBits number of bits to store uncompressed.
 * 
 * \return null: error occurred
 *         otherwise: newly allocated dictionary
*/
MarlinDictionary *Marlin_build_dictionary(const char *name, const double hist[256]);

/*! 
 * Frees a previously built Marlin Dictionary
 * 
 * \param dict dictionary to free
*/
void Marlin_free_dictionary(MarlinDictionary *dict);

/*! 
 * Obtains a set of pre-built dictionaries (THose must not be freed).
 * 
 * \return pointer to a vector of dictionary pointers ended in nullptr
*/
MarlinDictionary **Marlin_get_prebuilt_dictionaries();

/*! 
 * Estimates how much space a dictionary will take to compress a source with a histogram hist.
 * 
 * \return negative: error occurred
 *         positive: expected space used to compress hist using dictionary dict
*/
double Marlin_estimate_space(MarlinDictionary *dict, const double hist[256]);

#if defined (__cplusplus)
}

#include <vector>
#include <map>
#include <memory>
	
struct MarlinDictionary {

	/// Configuration map
	typedef std::map<std::string, double> Configuration;
	const Configuration conf;


	/// Main Typedefs
	typedef uint8_t  SourceSymbol; // Type taht store the raw symbols from the source.
	struct MarlinSymbol {
		SourceSymbol sourceSymbol;
		double p;
	};

	typedef uint8_t  MarlinIdx; // Type taht store the raw symbols from the source.
	struct Word : std::vector<MarlinIdx> {
		using std::vector<MarlinIdx>::vector;
		double p = 0;
		MarlinIdx state = 0;
	};
	
	/// BASIC CONFIGURATION
	const size_t K                = conf.at("K");           // Non overlapping bits of codeword.
	const size_t O                = conf.at("O");           // Bits that overlap between codewprds.
	const size_t shift            = conf.at("shift");       // Bits that can be stored raw
	const size_t maxWordSize      = conf.at("maxWordSize"); // Maximum number of symbols per word.

	/// ALPHABETS
	const std::vector<double> sourceAlphabet;	
	const std::vector<MarlinSymbol> marlinAlphabet = buildMarlinAlphabet();
		
	/// DICTIONARY
	//Marlin only encodes a subset of the possible source symbols.
	//Marlin symbols are sorted by probability in descending order, 
	//so the Marlin Symbol 0 is always corresponds to the most probable alphabet symbol.
	const std::vector<Word> words = buildDictionary(); // All dictionary words.
	const double efficiency       = calcEfficiency();  // Theoretical efficiency of the dictionary.
	
	/// DECOMPRESSOR STUFF
	const std::unique_ptr<std::vector<SourceSymbol>> decompressorTableVector = buildDecompressorTable();	
	const SourceSymbol* const decompressorTablePointer = decompressorTableVector->data();
	ssize_t decompress(uint8_t* dst, size_t dstSize, const uint8_t* src, size_t srcSize) const;
	ssize_t decompress(const std::vector<uint8_t> &src, std::vector<uint8_t> &dst) const {
		return decompress(dst.data(), dst.size(), src.data(), src.size());
	}
	
	/// COMPRESSOR STUFF
	typedef uint32_t CompressorTableIdx;      // Structured as: FLAG_NEXT_WORD Where to jump next	
	const std::unique_ptr<std::vector<CompressorTableIdx>> compressorTableVector = buildCompressorTable();	
	const CompressorTableIdx* const compressorTablePointer = compressorTableVector->data();	
	ssize_t compress(uint8_t* dst, size_t dstCapacity, const uint8_t* src, size_t srcSize) const;
	ssize_t compress(const std::vector<uint8_t> &src, std::vector<uint8_t> &dst) const {
		ssize_t r = compress(dst.data(), dst.capacity(), src.data(), src.size());
		if (r<0) return r;
		dst.resize(r);
		return dst.size();
	}


	/// CONSTRUCTORS
	MarlinDictionary( const std::vector<double> &sourceAlphabet_,
		Configuration conf_ = Configuration()) 
		: conf(updateConf(sourceAlphabet_, conf_)), sourceAlphabet(sourceAlphabet_) {}
		
private:
	// Sets default configurations
	static std::map<std::string, double> updateConf(const std::vector<double> &sourceAlphabet, Configuration conf);

	std::vector<MarlinSymbol> buildMarlinAlphabet() const;
	
	std::vector<Word> buildDictionary() const;
	double calcEfficiency() const;

	std::unique_ptr<std::vector<SourceSymbol>> buildDecompressorTable() const;
	std::unique_ptr<std::vector<CompressorTableIdx>> buildCompressorTable() const;
};
#endif

#endif

