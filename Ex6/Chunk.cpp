#include "Chunk.hpp"
#include "Wolf3D.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

Chunk::Chunk(glm::mat4 chunkTransform){

	//testBlock = new Block();

	//Set the position of the chunk (Upper, front, left corner, hopefully)
	this->chunkTransform = chunkTransform;
	
	//Debugging
	//const float *pSource = (const float*)glm::value_ptr(chunkTransform);
	//for (int i = 0; i < 16; ++i) //Output from upper left corner, one collum at a time
	//	std::cout << pSource[i] << std::endl;
	//std::cout << "END OF MATRIX" << std::endl;

	// Create the blocks
	blocksInChunk = new Block** [chunkDimensions];
	for (int i = 0; i < chunkDimensions; i++){
		blocksInChunk[i] = new Block* [chunkDimensions];
		for (int j = 0; j < chunkDimensions; j++){
			blocksInChunk[i][j] = new Block[chunkDimensions];
		}
	}
}

Chunk::~Chunk(){
	// Delete the blocks
	for (int i = 0; i < chunkDimensions; ++i){
		for (int j = 0; j < chunkDimensions; ++j){
			delete[] blocksInChunk[i][j];
		}
		delete[] blocksInChunk[i];
	}
	delete[] blocksInChunk;
}

void Chunk::update(float dt) {
	//Dunno what to do in here yet
}

void Chunk::draw(sre::RenderPass& renderpass) {
	//Loop over all cubes, get their information, call render
	//Offset blocks by a transformation every time

	//glm::translate(vec3(10.0f, 0.0f, -10.0f));
	//renderpass.draw(floor, floorTransform, floorMat);

	//#IMPORTANT: We asume that the block size is 1.0f

	//renderpass.draw(testBlock->getMesh(), chunkTransform, Wolf3D::getInstance()->blockMaterial);

	for (int x = 0; x < chunkDimensions; x++) {
		for (int y = 0; y < chunkDimensions; y++) {
			for (int z = 0; z < chunkDimensions; z++) {
				auto transformMatrix = glm::translate(chunkTransform, glm::vec3(x * 2, y * 2, z * 2)); // HACK block size is set to two

				renderpass.draw(blocksInChunk[x][y][z].getMesh(), transformMatrix, Wolf3D::getInstance()->blockMaterial);
			}
		}
	}
}