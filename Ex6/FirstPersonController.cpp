//
// Created by Morten Nobel-Jørgensen on 29/09/2017.
//
#include "FirstPersonController.hpp"
#include <glm/gtx/rotate_vector.hpp>
#include "Wolf3D.hpp"

using namespace sre;
using namespace glm;


FirstPersonController::FirstPersonController(sre::Camera * camera)
:camera(camera) {
	// Setup  Camera projection
    camera->setPerspectiveProjection(FIELD_OF_FIELD, NEAR_PLANE, FAR_PLANE);

	// Create Capsule collider
	// # TODO PRIORITY fix controller shape size without getting stuck..
	btCollisionShape* controllerShape = new btCapsuleShape(COLLIDER_RADIUS, COLLIDER_HEIGHT);
	btDefaultMotionState* motionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0)));

	btScalar mass = 1;
	btVector3 fallInertia(0, 0, 0);
	controllerShape->calculateLocalInertia(mass, fallInertia);

	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(mass, motionState, controllerShape, fallInertia);
	rigidBody = new btRigidBody(fallRigidBodyCI);
	rigidBody->setFriction(0.0f);

	// Disable the rigidbody from going to sleep
	rigidBody->setActivationState(DISABLE_DEACTIVATION);

	// Add rigidbody to world
	Wolf3D::getInstance()->physics.addRigidBody(rigidBody);

	// Only allow for rotations around the Y-axis
	rigidBody->setAngularFactor(btVector3(0, 1, 0));

	// Set initial look rotation to 0, 0
	lookRotation = vec2(0,0);

	// Set local block matrix to display the block in the bottom right corner in front of the camera
	handBlockOffsetMatrix = translate(mat4(), vec3(0.5f, -.5f, -1));
}


FirstPersonController::~FirstPersonController(){
	// TODO check if done correctly
//	delete rigidBody;
//	delete motionState;
//	delete controllerShape;
}



void FirstPersonController::update(float deltaTime){

	// Determine local movement
	vec3 movement = vec3(0, 0, 0);

	// Only handle movement if we are grounded
	if(isGrounded || !NEEDS_GROUNDED_TO_MOVE){
		if(fwd)
			movement += vec3(0, 0, -1);
		if(left)
			movement += vec3(-1, 0, 0);
		if(bwd)
			movement += vec3(0, 0, 1);
		if(right)
			movement += vec3(1, 0, 0);

		// Only normalize if we have some movement.
		// Crashes if we try to normalize (0, 0, 0).
		if (movement != vec3(0, 0, 0))
			movement = glm::normalize(movement);

		if(isSprinting)
			movement *= SPRINT_MOVEMENT_INCREASE;	

		if(!isGrounded)
			movement *= JUMP_MOVEMENT_MULTIPLIER;

		if (flyMode) {
			if (up)
				movement += vec3(0, 1, 0);
			if (down)
				movement += vec3(0, -1, 0);
		}

		// Multiply by time that has passed to compensate for FPS
		movement *= deltaTime;


		// TODO use collider rotations? Is this necessary?
		// Translate local movement to relative world movement 
		float x = cos(radians(lookRotation.x)) * movement.x - sin(radians(lookRotation.x)) * movement.z;
		float z = cos(radians(lookRotation.x)) * movement.z + sin(radians(lookRotation.x)) * movement.x;

		// Apply movmement
		btVector3 velocity = rigidBody->getLinearVelocity();
		if(flyMode)
			velocity = btVector3(x * MOVEMENT_SPEED, movement.y * MOVEMENT_SPEED, z * MOVEMENT_SPEED); // Carry falling speed to our current movement
		else
			velocity = btVector3(x * MOVEMENT_SPEED, velocity.getY(), z * MOVEMENT_SPEED); // Carry falling speed to our current movement
		rigidBody->setLinearVelocity(velocity);
	}
	
	// Get our position from physics
	btTransform transform;
	rigidBody->getMotionState()->getWorldTransform(transform);
	btVector3 position = transform.getOrigin();

	// Check if the controller is grounded
	checkGrounded(position);
	
	// Update our tranform matrix, pass it on to the camera
	transformMatrix = mat4();
	transformMatrix = translate(transformMatrix, glm::vec3(position.getX(), position.getY() + Y_CAMERA_OFFSET, position.getZ())); 
	transformMatrix = rotate(transformMatrix, radians(lookRotation.x), vec3(0, -1, 0));
	transformMatrix = rotate(transformMatrix, radians(lookRotation.y), vec3(-1, 0, 0));
	camera->setViewTransform(glm::inverse(transformMatrix));

	// Mine block
	if (isMining) {
		auto detectedBlock = castRayForBlock(-0.2f);

		if(detectedBlock != nullptr && detectedBlock == lastBlock) {
			minedAmount += deltaTime;

			if (minedAmount >= 1) {
				destroyBlock(lastBlock);
			}
		} else {
			minedAmount = 0;
		}

		lastBlock = detectedBlock;
	}
}


void FirstPersonController::draw(sre::RenderPass& renderpass) {
	// Translate to  the correct position ( controller position + the block offset for the hand)
	auto matrix = transformMatrix * handBlockOffsetMatrix;
	// Scale the block to 35%
	matrix = scale(matrix, vec3(0.35f, 0.35f, 0.35f));
	// Rotate it 45 degrees around y
	matrix = rotate(matrix, radians(45.0f), vec3(0, -1, 0));
	
	// Draw the block we currently have selected
	renderpass.draw(Wolf3D::getInstance()->getBlockMesh(blockSelected), matrix, Wolf3D::getInstance()->blockMaterial);
}


glm::vec3 FirstPersonController::getPosition() {
	btTransform transform;
	rigidBody->getMotionState()->getWorldTransform(transform);
	btVector3 position = transform.getOrigin();
	return glm::vec3(position.getX(), position.getY(), position.getZ());
}


void FirstPersonController::checkGrounded(btVector3 position) {
	// Cast a ray from our position to a location far below it
	btVector3 down = position + btVector3(0, -100000, 0);
	btCollisionWorld::ClosestRayResultCallback res(position, down);

	Wolf3D::getInstance()->physics.raycast(&position, &down, &res);

	// If we hit something, check the distance to the ground
	// TODO distance check - expansive calculation, change to something more efficient.
	if (res.hasHit()) {
		isGrounded = !(position.distance(res.m_hitPointWorld) > COLLIDER_RADIUS + 0.5f * COLLIDER_HEIGHT + 0.1f); 
	}
	else {
		isGrounded = false;
	}
}


void FirstPersonController::onKey(SDL_Event &event) {
	// Move character position if HOME is pressed
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_HOME) {
		setPosition(glm::vec3(0, 8, 0), 0);
	}


	// Toggle Replacement mode of placing blocks
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_r) {
		replaceBlock = !replaceBlock;
	}

	// Toggle invisible mode
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_t) {
		ghostMode = !ghostMode;

		// Ignore contact respones if invisible mode is 
		if(ghostMode)
			rigidBody->setCollisionFlags(btCollisionObject::CollisionFlags::CF_NO_CONTACT_RESPONSE);
		else
			rigidBody->setCollisionFlags(0);
	}

	// Toggle flying
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_y) {
		flyMode = !flyMode;
		if(flyMode)
			rigidBody->setGravity(btVector3(0, 0, 0));
		else
			rigidBody->setGravity(btVector3(0, -10, 0));
	} 


	// Capture Jump TODO set back to force
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE && isGrounded) {
		if (!flyMode)
			rigidBody->setLinearVelocity(btVector3(0, 5, 0)); //applyCentralForce(btVector3(0, JUMP_FORCE, 0));
			//rigidBody->applyCentralForce(btVector3(0,JUMP_FORCE,0));
	}

	// Selected block to place
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_e) {
		// Increase block selected
		blockSelected = (BlockType)(blockSelected + 1);

		// If we have the last block selected, go back to the start
		if (blockSelected == BlockType::LENGTH)
			blockSelected = BlockType::Stone;
	}

	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_q) {
		// If we are at the end, go back to the start
		if (blockSelected == BlockType::Stone)
			blockSelected = BlockType::LENGTH;

		// Decrease block selected
		blockSelected = (BlockType)(blockSelected - 1);
	}


	// Capture movement keys down
    if(event.type == SDL_KEYDOWN ){
		switch (event.key.keysym.sym){
			case SDLK_w:
				fwd = true;
				break;
			case SDLK_a:
				left = true;
				break;
			case SDLK_s:
				bwd = true;
				break;
			case SDLK_d:
				right = true;
				break;
			case SDLK_LSHIFT:
				isSprinting = true;
				camera->setPerspectiveProjection(FIELD_OF_FIELD * SPRINT_FOV_INCREASE, NEAR_PLANE, FAR_PLANE);
				break;
			case SDLK_SPACE:
				up = true;
				break;
			case SDLK_LCTRL:
				down = true;
				break;
		}
	}
	// Capture movement keys released
	if (event.type == SDL_KEYUP) {
		switch (event.key.keysym.sym) {
			case SDLK_w:
				fwd = false;
				break;
			case SDLK_a:
				left = false;
				break;
			case SDLK_s:
				bwd = false;
				break;
			case SDLK_d:
				right = false;
				break;
			case SDLK_LSHIFT:
				camera->setPerspectiveProjection(FIELD_OF_FIELD, NEAR_PLANE, FAR_PLANE);
				isSprinting = false;
				break;
			case SDLK_SPACE:
				up = false;
				break;
			case SDLK_LCTRL:
				down = false;
				break;
			}
	}
}


// Handle Mouse Events
void FirstPersonController::onMouse(SDL_Event &event) {
	if(event.type == SDL_MOUSEMOTION && !lockRotation) {
		lookRotation.x += event.motion.xrel * ROTATION_SPEED;
		lookRotation.y += event.motion.yrel * ROTATION_SPEED;
	lookRotation.y = clamp(lookRotation.y, -MAX_X_LOOK_UP_ROTATION, MAX_X_LOOK_DOWN_ROTATION);
	}
	
	if (event.type == SDL_MOUSEBUTTONDOWN) {
		if (event.button.button == SDL_BUTTON_LEFT) {
			isMining = true;
		}
		else if(event.button.button == SDL_BUTTON_RIGHT) {
			placeBlock();
		}
	}
	if (event.type == SDL_MOUSEBUTTONUP) {
		if (event.button.button == SDL_BUTTON_LEFT) {
			isMining = false;
			minedAmount = 0;
		}
	}
}


void FirstPersonController::destroyBlock(Block* block) {
	vec3 position = block->getPosition();
	position /= Chunk::getChunkDimensions();
	Wolf3D::getInstance()->getChunk((int)position.x, (int)position.y, (int)position.z)->flagRecalculateMesh();

	block->setActive(false);

	minedAmount = 0;
}


void FirstPersonController::placeBlock() {
	// Get the block where we are placing
	// If we replacemode is active replace the actual block
	// Otherwise get the empty space we are looking at
	auto detectedBlock = castRayForBlock(replaceBlock? -.2f : .2f);

	// If we are looking at a block, place one
	if (detectedBlock != nullptr) {
		// If theres is already a block and we are not allowed to replace it, don't do anything
		if(!replaceBlock && detectedBlock->isActive())
			return;

		vec3 position = detectedBlock->getPosition();
		position /= Chunk::getChunkDimensions();
		Wolf3D::getInstance()->getChunk((int)position.x, (int)position.y, (int)position.z)->flagRecalculateMesh();

		detectedBlock->setType(blockSelected);
		detectedBlock->setActive(true);
	}	
}


Block* FirstPersonController::castRayForBlock(float normalMultiplier) {
	btVector3 start = rigidBody->getWorldTransform().getOrigin();
	start.setY(start.getY() + Y_CAMERA_OFFSET);
	std::cout << lookRotation.y << " - " << lookRotation.y / 45 << std::endl;

	//float lookY = 0;
	//float lookYa = 0;
	//float lookYb = 0;

	//if(lookRotation.y < 30 && lookRotation.y > -30)
	//	lookYa = sin(radians(lookRotation.y));
	//else 
	//	lookYb = (lookRotation.y /45);
	//float p = (!sign(lookRotation.y) ? lookRotation.y / MAX_X_LOOK_UP_ROTATION :  lookRotation.y / MAX_X_LOOK_DOWN_ROTATION);
	//lookY = (1 - p)*lookYa + (p) * lookYb;
	// float p = lookRotation.y - 45;
	// (lookRotation.y / 45) * (-1  )
	// float p = 0.25;
	// float lookY = (sin(radians(lookRotation.y)) * 1.25f) * p + (lookRotation.y/52) * (1-p);
	btVector3 direction = btVector3(sin(radians(lookRotation.x)), 0, cos(radians(lookRotation.x)) * -1);
	direction = direction.normalized();
	direction.setY(sin(radians(lookRotation.y)) * (-1.2));
	btVector3 end = start + direction * 10.0f;

	btCollisionWorld::ClosestRayResultCallback res(start, end);

	// Cast ray
	Wolf3D::getInstance()->physics.raycast(&start, &end, &res);

	// If we have an hit handle it, else return null
	if (res.hasHit()) {
		// Store hit location
		btVector3 hit = res.m_hitPointWorld;

		// TODO TEMP prob remove below -- added for debug purposes
		toRay = vec3(hit.getX(), hit.getY(), hit.getZ());
		toRay2 = toRay + vec3(res.m_hitNormalWorld.getX(), res.m_hitNormalWorld.getY(), res.m_hitNormalWorld.getZ()) * .2f; //res.m_hitNormalWorld;
		fromRay1 = toRay;
		fromRay = vec3(start.getX(), start.getY(), start.getZ());

		// Compensate for origin of block, collider origin is in center, though for the math we want it to be on a corner
		hit += btVector3(0.501f, 0.501f, 0.501f);

		// Every block is above or at 0. If we hit the bedrock it could be that we get below 0. So make really sure we always grab blocks on atleast 0
		if (hit.getY() < 0)
			hit.setY(0);

		// Add some of  the hit normal. The raycast always hits the edge of the collider. This increases accuracy since we move into the collider or out.
		hit += res.m_hitNormalWorld * normalMultiplier;

		// Grab the block, and set it to not active - all numbers are floored since blocks take up a whole unit
		return Wolf3D::getInstance()->locationToBlock((int)hit.getX(), (int)hit.getY(), (int)hit.getZ(), true);
	} else{
		return nullptr;
	}
}


// Set Spawn Position
void FirstPersonController::setPosition(glm::vec3 position, float rotation) {
    this->lookRotation.x = rotation;
	this->lookRotation.y = 0;
	rigidBody->translate(btVector3(position.x, position.y, position.z));
}


bool FirstPersonController::getIsGrounded() {
	return isGrounded;
}

float FirstPersonController::getMinedAmount() {
	return minedAmount;
}