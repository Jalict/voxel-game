/*
* Initially Created by Morten Nobel-Jørgensen on 29/09/2017.
*
* FirstPersonController - included in project from: 30-11-2017
* Wrapper for all the bullet physics.
*/
#include <glm/gtx/rotate_vector.hpp>
#include "FirstPersonController.hpp"
#include "Game.hpp"

using namespace sre;
using namespace glm;



FirstPersonController::FirstPersonController(sre::Camera * camera)
:camera(camera) {
	// Setup  Camera projection
    camera->setPerspectiveProjection(FIELD_OF_VIEW, NEAR_PLANE, FAR_PLANE);

	// Create Capsule collider
	controllerShape = new btCapsuleShape(COLLIDER_RADIUS, COLLIDER_HEIGHT);
	btDefaultMotionState* motionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0)));

	btScalar mass = 1;
	btVector3 fallInertia(0, 0, 0);
	controllerShape->calculateLocalInertia(mass, fallInertia);

	btRigidBody::btRigidBodyConstructionInfo fallRigidBodyCI(mass, motionState, controllerShape, fallInertia);
	rigidBody = new btRigidBody(fallRigidBodyCI);

	// Disable friction for the character controller
	rigidBody->setFriction(0.0f);

	// Disable the rigidbody from going to sleep
	rigidBody->setActivationState(DISABLE_DEACTIVATION);

	// Add rigidbody to world
	Game::getInstance()->getPhysics()->addRigidBody(rigidBody);

	// Only allow for rotations around the Y-axis
	rigidBody->setAngularFactor(btVector3(0, 1, 0));

	// Set initial look rotation to 0, 0
	lookRotation = vec2(0,0);

	// Set local block matrix to display the block in the bottom right corner in front of the camera
	handBlockOffsetMatrix = translate(mat4(), vec3(0.5f, -.5f, -1));
}


FirstPersonController::~FirstPersonController(){
	Game::getInstance()->getPhysics()->removeRigidBody(rigidBody);
	delete rigidBody->getMotionState();
	delete rigidBody;
	delete controllerShape;
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

			if (minedAmount >= 1 || instantMining) {
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
	renderpass.draw(Game::getInstance()->getBlockMesh(blockSelected), matrix, Game::getInstance()->getBlockMaterial());

	// Draw the raycasts which are used for looking if enabled
	if (drawLookRays) {
		std::vector<vec3> rays;
		rays.push_back(fromRay);
		rays.push_back(toRay);
		renderpass.drawLines(rays);

		std::vector<vec3> rays1;
		rays1.push_back(fromRayNormal);
		rays1.push_back(toRayNormal);
		renderpass.drawLines(rays1, vec4(1, 0, 0, 1));
	}
}


void FirstPersonController::checkGrounded(btVector3 position) {
	// Cast a ray from our position to a location far below it
	btVector3 down = position + btVector3(0, -100000, 0);
	btCollisionWorld::ClosestRayResultCallback res(position, down);

	Game::getInstance()->getPhysics()->raycast(&position, &down, &res);

	// If we hit something, check the distance to the ground
	if (res.hasHit()) {
		isGrounded = !(position.distance(res.m_hitPointWorld) > COLLIDER_RADIUS + 0.5f * COLLIDER_HEIGHT + 0.1f); // Add 0.1f to make sure we are in air.
	}
	else {
		isGrounded = false;
	}
}


void FirstPersonController::onKey(SDL_Event &event) {
	// Teleport the controller up if HOME is pressed
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_HOME) {
		translateController(glm::vec3(0, 8, 0), 0);
	}


	// Toggle Replacement mode of placing blocks
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_r) {
		replaceBlock = !replaceBlock;

		if(replaceBlock)
			std::cout << " Replace blocks is now activated" << std::endl;
		else
			std::cout << " Replace blocks is now deactivated" << std::endl;
	}

	// Toggle invisible mode
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_t) {
		ghostMode = !ghostMode;

		// Ignore contact respones if invisible mode is 
		if (ghostMode) {
			rigidBody->setCollisionFlags(btCollisionObject::CollisionFlags::CF_NO_CONTACT_RESPONSE);
			std::cout << " Ghost mode is now activated" << std::endl;
		}
		else {
			rigidBody->setCollisionFlags(0);
			std::cout << " Ghost mode is now deactivated" << std::endl;
		}
	}

	// Toggle flying
	if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_y) {
		flyMode = !flyMode;
		if (flyMode) {
			std::cout << " Fly mode is now activated" << std::endl;
			rigidBody->setGravity(btVector3(0, 0, 0));
		}
		else {
			std::cout << " Fly mode is now deactivated" << std::endl;
			rigidBody->setGravity(btVector3(0, -10, 0));
		}
	} 

	// Toggle instant mining
		if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_u) {
			instantMining = !instantMining;
			if (instantMining) {
				std::cout << " Instant mining is now activated" << std::endl;
			}
			else {
				std::cout << " instant mining is now deactivated" << std::endl;
			}
		}

	// Activate jump
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE && isGrounded) {
		if (!flyMode)
			rigidBody->setLinearVelocity(btVector3(0, 5, 0)); 
	}


	// Change selected block that is in the hand of the controller
	if (event.type == SDL_KEYUP) {
		if(event.key.keysym.sym == SDLK_e){
			// Increase block selected
			blockSelected = (BlockType)(blockSelected + 1);

			// If we have the last block selected, go back to the start
			if (blockSelected == BlockType::LENGTH)
				blockSelected = BlockType::Stone;
		}
		else if (event.key.keysym.sym == SDLK_q) {
			// If we are at the end, go back to the start
			if (blockSelected == BlockType::Stone)
				blockSelected = BlockType::LENGTH;

			// Decrease block selected
			blockSelected = (BlockType)(blockSelected - 1);
		}
	}


	// Capture movement, flying controls and springing keys down
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
				camera->setPerspectiveProjection(FIELD_OF_VIEW * SPRINT_FOV_INCREASE, NEAR_PLANE, FAR_PLANE);
				break;
			case SDLK_SPACE:
				up = true;
				break;
			case SDLK_LCTRL:
				down = true;
				break;
		}
	}

	// Capture movement, flying controls and springing keys released
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
				camera->setPerspectiveProjection(FIELD_OF_VIEW, NEAR_PLANE, FAR_PLANE);
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


void FirstPersonController::onMouse(SDL_Event &event) {
	// Apply mouse movement to rotations
	if(event.type == SDL_MOUSEMOTION && !lockRotation) {
		lookRotation.x += event.motion.xrel * ROTATION_SPEED;
		lookRotation.y += event.motion.yrel * ROTATION_SPEED;
		lookRotation.y = clamp(lookRotation.y, -MAX_X_LOOK_UP_ROTATION, MAX_X_LOOK_DOWN_ROTATION);
	}
	
	// Handle mouse clicks
	if (event.type == SDL_MOUSEBUTTONDOWN) {
		// When left mouse button is pressed start mining
		if (event.button.button == SDL_BUTTON_LEFT) {
			isMining = true;
		}
		// When right mouse is pressed place a block
		else if(event.button.button == SDL_BUTTON_RIGHT) {
			placeBlock();
		}
	}
	if (event.type == SDL_MOUSEBUTTONUP) {
		// When released reset mining progress
		if (event.button.button == SDL_BUTTON_LEFT) {
			isMining = false;
			minedAmount = 0;
		}
	}
}


void FirstPersonController::destroyBlock(Block* block) {
	// Get the location of the block we are looking at
	vec3 position = block->getPosition();
	
	// Deactivate the block we destroyed
	block->setActive(false);

	// Reset mining progress
	minedAmount = 0;

	// Flag the necessary chunks for recalculation of mesh
	Game::getInstance()->flagNeighboursForRecalculateIfNecessary(position.x, position.y, position.z);
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
		Game::getInstance()->flagNeighboursForRecalculateIfNecessary((int)position.x, (int)position.y, (int)position.z);

		detectedBlock->setType(blockSelected);
		detectedBlock->setActive(true);
	}	
}


Block* FirstPersonController::castRayForBlock(float normalMultiplier) {
	btVector3 start = rigidBody->getWorldTransform().getOrigin();
	start.setY(start.getY() + Y_CAMERA_OFFSET);

	float cosY = cos(radians(lookRotation.y));
	btVector3 direction = btVector3( cosY * sin(radians(lookRotation.x)), -1 * sin(radians(lookRotation.y)), cosY * cos(radians(lookRotation.x)) * -1);
	direction = direction.normalized();

	btVector3 end = start + direction * MINE_RANGE;

	btCollisionWorld::ClosestRayResultCallback res(start, end);

	// Cast ray
	Game::getInstance()->getPhysics()->raycast(&start, &end, &res);

	// If we have an hit handle it, else return null
	if (res.hasHit()) {
		// Store hit location
		btVector3 hit = res.m_hitPointWorld;

		toRay = vec3(hit.getX(), hit.getY(), hit.getZ());
		toRayNormal = toRay + vec3(res.m_hitNormalWorld.getX(), res.m_hitNormalWorld.getY(), res.m_hitNormalWorld.getZ()) * .2f; //res.m_hitNormalWorld;
		fromRayNormal = toRay;
		fromRay = vec3(start.getX(), start.getY(), start.getZ());

		// Compensate for origin of block, collider origin is in center, though for the math we want it to be on a corner
		hit += btVector3(0.501f, 0.501f, 0.501f);

		// Every block is above or at 0. If we hit the bedrock it could be that we get below 0. So make really sure we always grab blocks on atleast 0
		if (hit.getY() < 0)
			hit.setY(0);

		// Add some of  the hit normal. The raycast always hits the edge of the collider. This increases accuracy since we move into the collider or out.
		hit += res.m_hitNormalWorld * normalMultiplier;

		// Grab the block, and set it to not active - all numbers are floored since blocks take up a whole unit
		return Game::getInstance()->locationToBlock((int)hit.getX(), (int)hit.getY(), (int)hit.getZ(), true);
	} else{
		return nullptr;
	}
}


void FirstPersonController::setLockRotation(bool lockRotation) {
	this->lockRotation = lockRotation;
}


void FirstPersonController::translateController(glm::vec3 position, float rotation) {
    this->lookRotation.x = rotation;
	this->lookRotation.y = 0;
	rigidBody->translate(btVector3(position.x, position.y, position.z));
}


glm::vec3 FirstPersonController::getPosition() {
	btTransform transform;
	rigidBody->getMotionState()->getWorldTransform(transform);
	btVector3 position = transform.getOrigin();
	return glm::vec3(position.getX(), position.getY(), position.getZ());
}