import sys
import pygame

WIN_W, WIN_H = 1024, 768
FPS = 60
SKY_COLOR = (135, 180, 220)
GROUND_COLOR = (80, 120, 60)
HUD_COLOR = (20, 20, 30)


def main():
    pygame.init()
    clock = pygame.time.Clock()
    screen = pygame.display.set_mode((WIN_W, WIN_H))
    pygame.display.set_caption("AABB Automata")

    ground_h = 80

    running = True
    paused = False

    while running:
        dt = clock.tick(FPS) / 1000.0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                if event.key == pygame.K_SPACE:
                    paused = not paused
                if event.key == pygame.K_r:
                    pass

        # Draw sky
        screen.fill(SKY_COLOR)

        # Draw ground
        pygame.draw.rect(screen, GROUND_COLOR,
                         (0, WIN_H - ground_h, WIN_W, ground_h))

        # HUD
        pygame.draw.rect(screen, HUD_COLOR, (0, 0, WIN_W, 36))
        font = pygame.font.Font(None, 22)
        status = "PAUSED" if paused else "RUNNING"
        hud_text = font.render(f"AABB Automata  |  {status}  |  Space: pause  |  R: restart", True, (200, 200, 200))
        screen.blit(hud_text, (12, 8))

        pygame.display.flip()

    pygame.quit()
    sys.exit()


if __name__ == "__main__":
    main()
